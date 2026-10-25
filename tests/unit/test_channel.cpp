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

struct RecordingCallback : public IAirServerCallback {
	int frames = 0;
	unsigned width = 0, height = 0;
	uint8_t y00 = 0, y160_120 = 0, u160_120 = 0, v160_120 = 0;

	void connected(const char*, const char*) {}
	void disconnected(const char*, const char*) {}
	void outputAudio(SFgAudioFrame*, const char*, const char*) {}
	void outputVideo(SFgVideoFrame* f, const char*, const char*) {
		if (frames++ == 0) {
			width = f->width;
			height = f->height;
			const uint8_t* y = f->data;
			const uint8_t* uv = f->data + f->dataLen[0];
			y00 = y[0];
			y160_120 = y[120 * f->pitch[0] + 160];
			u160_120 = uv[60 * f->pitch[1] + 160];
			v160_120 = uv[60 * f->pitch[1] + 161];
		}
	}
	void audioSetVolume(float, const char*, const char*) {}
	void audioSetMetadata(const void*, int, const char*, const char*) {}
	void audioSetCoverArt(const void*, int, const char*, const char*) {}
	void videoPlay(char*, double, double) {}
	void videoGetPlayInfo(double* d, double* p, double* r) { *d = *p = *r = 0; }
	void log(int, const char*) {}
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

TEST_CASE("channel decodes mirror packets and reports NV12 frames")
{
	std::vector<std::vector<uint8_t>> nals = nal_units(read_clip());
	REQUIRE(nals.size() > 10);

	RecordingCallback cb;
	FgAirplayChannel* channel = new FgAirplayChannel(&cb);

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
	CHECK(cb.y00 == 16);
	CHECK(cb.y160_120 == 210);
	CHECK(cb.u160_120 == 16);
	CHECK(cb.v160_120 == 146);
}
