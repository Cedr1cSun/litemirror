// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// FgAirplayChannel is the glue between the mirror stream and the decoder:
// the first packet carries SPS/PPS (is_key), later ones a frame each. The
// callback must see one NV12 picture per frame with ffmpeg's pixel values.
#include "doctest.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include "FgAirplayChannel.h"
#include <dxgi1_2.h>
#include "LmVideoSink.h"

static std::vector<uint8_t> read_clip()
{
	const char* paths[] = { "tests/unit/data/clip.h264", "../../../tests/unit/data/clip.h264" };
	for (const char* p : paths) {
		FILE* f = fopen(p, "rb");
		if (!f) continue;
		std::vector<uint8_t> buf;
		uint8_t tmp[4096];
		size_t n;
		while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
		fclose(f);
		return buf;
	}
	return {};
}

// NAL units, each with its start code, in stream order
static std::vector<std::vector<uint8_t>> nal_units(const std::vector<uint8_t>& s)
{
	std::vector<size_t> starts;
	for (size_t i = 0; i + 3 < s.size(); i++) {
		if (s[i] == 0 && s[i + 1] == 0 && (s[i + 2] == 1 || (s[i + 2] == 0 && s[i + 3] == 1))) {
			starts.push_back(i);
			i += s[i + 2] == 1 ? 2 : 3;
		}
	}
	starts.push_back(s.size());
	std::vector<std::vector<uint8_t>> out;
	for (size_t k = 0; k + 1 < starts.size(); k++)
		out.emplace_back(s.begin() + starts[k], s.begin() + starts[k + 1]);
	return out;
}

static int nal_type(const std::vector<uint8_t>& n) { return n[n[2] == 1 ? 3 : 4] & 0x1f; }

struct Recording {
	int frames = 0;
	int sizes = 0;   // on_video_size calls
	int stats = 0;   // on_stats calls
	lm_stats last = {};
	unsigned width = 0, height = 0;
	unsigned sizeWidth = 0, sizeHeight = 0;
	uint8_t y00 = 0, y160_120 = 0, u160_120 = 0, v160_120 = 0;

	static void onVideo(void* user, const char*, const lm_video_frame* f) {
		Recording* self = (Recording*)user;
		if (self->frames++ == 0) {
			self->width = f->width;
			self->height = f->height;
			const uint8_t* y = f->plane[0];
			const uint8_t* uv = f->plane[1];
			self->y00 = y[0];
			self->y160_120 = y[120 * f->pitch[0] + 160];
			self->u160_120 = uv[60 * f->pitch[1] + 160];
			self->v160_120 = uv[60 * f->pitch[1] + 161];
		}
	}
	static void onVideoSize(void* user, const char*, uint32_t w, uint32_t h) {
		Recording* self = (Recording*)user;
		self->sizes++;
		self->sizeWidth = w;
		self->sizeHeight = h;
	}
	static void onStats(void* user, const char*, const lm_stats* s) {
		Recording* self = (Recording*)user;
		self->stats++;
		self->last = *s;
	}
	lm_callbacks callbacks() {
		lm_callbacks cb;
		memset(&cb, 0, sizeof(cb));
		cb.user = this;
		cb.on_video = onVideo;
		cb.on_video_size = onVideoSize;
		cb.on_stats = onStats;
		return cb;
	}
};

static SFgH264Data packet(std::vector<uint8_t>& bytes, int is_key, int pts)
{
	SFgH264Data d;
	memset(&d, 0, sizeof(d));
	d.data = bytes.data();
	d.size = (int)bytes.size();
	d.is_key = is_key;
	d.pts = pts;
	return d;
}

// Feed the clip the way the mirror stream arrives: SPS+PPS as one key packet, then one packet per frame
static void feed_clip(FgAirplayChannel* channel)
{
	std::vector<std::vector<uint8_t>> nals = nal_units(read_clip());
	REQUIRE(nals.size() > 10);
	int pts = 0;
	std::vector<uint8_t> params, frame;
	for (size_t i = 0; i < nals.size(); i++) {
		int t = nal_type(nals[i]);
		if (t == 7 || t == 8) {
			params.insert(params.end(), nals[i].begin(), nals[i].end());
			if (t == 8) {
				SFgH264Data key = packet(params, 1, pts);
				channel->decodeH264Data(&key, "TestPhone", "AA:BB:CC:DD:EE:FF");
				params.clear();
			}
			continue;
		}
		frame.assign(nals[i].begin(), nals[i].end());
		if (t == 6) {
			frame.insert(frame.end(), nals[i + 1].begin(), nals[i + 1].end());
			i++;
		}
		SFgH264Data d = packet(frame, 0, pts);
		channel->decodeH264Data(&d, "TestPhone", "AA:BB:CC:DD:EE:FF");
		pts += 33333;
	}
}

TEST_CASE("channel with hardware decoding off still reports frames with the right pixels")
{
	Recording cb;
	lm_callbacks callbacks = cb.callbacks();
	FgAirplayChannel* channel = new FgAirplayChannel(&callbacks, NULL, false);
	feed_clip(channel);
	channel->release();
	CHECK(cb.frames == 10);
	CHECK(cb.width == 320);
	CHECK(cb.y00 == 16);
	CHECK(cb.y160_120 == 210);
	CHECK(cb.u160_120 == 16);
	CHECK(cb.v160_120 == 146);
}

TEST_CASE("channel with hardware decoding off still draws into the window sink")
{
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 320, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	UINT before = 0;
	sink.swapChain()->GetLastPresentCount(&before);

	Recording cb;
	lm_callbacks callbacks = cb.callbacks();
	FgAirplayChannel* channel = new FgAirplayChannel(&callbacks, &sink, false);
	feed_clip(channel);
	channel->release();

	UINT after = 0;
	sink.swapChain()->GetLastPresentCount(&after);
	CHECK(after - before == 10);   // every software frame was uploaded and shown
	CHECK(cb.frames == 0);
	sink.close();
	DestroyWindow(hwnd);
}

TEST_CASE("channel reports frame rate and processing time about once a second")
{
	Recording cb;
	lm_callbacks callbacks = cb.callbacks();
	FgAirplayChannel* channel = new FgAirplayChannel(&callbacks);
	feed_clip(channel);          // ten frames, as fast as they decode
	CHECK(cb.stats == 0);        // a second has not passed
	Sleep(1100);
	feed_clip(channel);          // the first frame after the second closes the period
	channel->release();

	REQUIRE(cb.stats >= 1);
	CHECK(cb.last.width == 320);
	CHECK(cb.last.height == 240);
	CHECK(cb.last.hardware == 1);
	CHECK(cb.last.fps > 5.0f);   // 10 frames over a little more than a second
	CHECK(cb.last.fps < 12.0f);
	CHECK(cb.last.process_ms > 0.0f);
	CHECK(cb.last.process_ms < 500.0f);
}

TEST_CASE("channel decodes mirror packets and reports NV12 frames")
{
	std::vector<std::vector<uint8_t>> nals = nal_units(read_clip());
	REQUIRE(nals.size() > 10);

	Recording cb;
	lm_callbacks callbacks = cb.callbacks();
	FgAirplayChannel* channel = new FgAirplayChannel(&callbacks);

	// The mirror stream sends SPS+PPS as one key packet, then one packet per
	// frame (SEI and slice together). Frames before the first key packet
	// must be ignored, not crash.
	std::vector<uint8_t> frame;
	frame.insert(frame.end(), nals[3].begin(), nals[3].end());   // the IDR slice alone
	SFgH264Data early = packet(frame, 0, 0);
	CHECK(channel->decodeH264Data(&early, "TestPhone", "AA:BB:CC:DD:EE:FF") == 0);
	CHECK(cb.frames == 0);

	int pts = 0;
	std::vector<uint8_t> params;
	for (size_t i = 0; i < nals.size(); i++) {
		int t = nal_type(nals[i]);
		if (t == 7 || t == 8) {
			params.insert(params.end(), nals[i].begin(), nals[i].end());
			if (t == 8) {
				SFgH264Data key = packet(params, 1, pts);
				channel->decodeH264Data(&key, "TestPhone", "AA:BB:CC:DD:EE:FF");
				params.clear();
			}
			continue;
		}
		frame.assign(nals[i].begin(), nals[i].end());
		if (t == 6) {   // SEI travels with the slice that follows
			frame.insert(frame.end(), nals[i + 1].begin(), nals[i + 1].end());
			i++;
		}
		SFgH264Data d = packet(frame, 0, pts);
		channel->decodeH264Data(&d, "TestPhone", "AA:BB:CC:DD:EE:FF");
		pts += 33333;
	}
	channel->release();

	CHECK(cb.frames == 10);
	CHECK(cb.width == 320);
	CHECK(cb.height == 240);
	CHECK(cb.sizes == 1);   // once, not per frame
	CHECK(cb.sizeWidth == 320);
	CHECK(cb.sizeHeight == 240);
	CHECK(cb.y00 == 16);
	CHECK(cb.y160_120 == 210);
	CHECK(cb.u160_120 == 16);
	CHECK(cb.v160_120 == 146);
}

TEST_CASE("channel with a window sink presents frames instead of calling on_video")
{
	std::vector<std::vector<uint8_t>> nals = nal_units(read_clip());
	REQUIRE(nals.size() > 10);
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 320, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	UINT before = 0;
	sink.swapChain()->GetLastPresentCount(&before);

	Recording cb;
	lm_callbacks callbacks = cb.callbacks();
	FgAirplayChannel* channel = new FgAirplayChannel(&callbacks, &sink);

	int pts = 0;
	std::vector<uint8_t> params, frame;
	for (size_t i = 0; i < nals.size(); i++) {
		int t = nal_type(nals[i]);
		if (t == 7 || t == 8) {
			params.insert(params.end(), nals[i].begin(), nals[i].end());
			if (t == 8) {
				SFgH264Data key = packet(params, 1, pts);
				channel->decodeH264Data(&key, "TestPhone", "AA:BB:CC:DD:EE:FF");
				params.clear();
			}
			continue;
		}
		frame.assign(nals[i].begin(), nals[i].end());
		if (t == 6) {
			frame.insert(frame.end(), nals[i + 1].begin(), nals[i + 1].end());
			i++;
		}
		SFgH264Data d = packet(frame, 0, pts);
		channel->decodeH264Data(&d, "TestPhone", "AA:BB:CC:DD:EE:FF");
		pts += 33333;
	}
	channel->release();

	UINT after = 0;
	sink.swapChain()->GetLastPresentCount(&after);
	CHECK(after - before == 10);
	CHECK(cb.frames == 0);
	CHECK(cb.sizes == 1);   // the shell still learns the picture size to shape its window
	CHECK(cb.sizeWidth == 320);
	CHECK(cb.sizeHeight == 240);
	sink.close();
	DestroyWindow(hwnd);
}
