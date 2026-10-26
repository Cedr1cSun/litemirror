// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Decodes tests/unit/data/clip.h264 (10 frames of ffmpeg's testsrc, 320x240,
// Annex B with SPS/PPS repeated) and compares pixels with what ffmpeg decodes.
// H.264 decoding is bit exact, so the values must match, not just be close.
#include "doctest.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "MfH264Decoder.h"

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

// Split the byte stream into access units: every NAL up to and including the
// next slice NAL (type 1 or 5) forms one unit, the way the mirror stream
// hands SPS+PPS+IDR to the decoder in one packet.
static std::vector<std::vector<uint8_t>> access_units(const std::vector<uint8_t>& s)
{
	std::vector<size_t> starts;
	for (size_t i = 0; i + 3 < s.size(); i++) {
		if (s[i] == 0 && s[i + 1] == 0 && (s[i + 2] == 1 || (s[i + 2] == 0 && s[i + 3] == 1))) {
			starts.push_back(i);
			i += s[i + 2] == 1 ? 2 : 3;
		}
	}
	starts.push_back(s.size());
	std::vector<std::vector<uint8_t>> units;
	std::vector<uint8_t> cur;
	for (size_t k = 0; k + 1 < starts.size(); k++) {
		size_t a = starts[k], b = starts[k + 1];
		cur.insert(cur.end(), s.begin() + a, s.begin() + b);
		size_t hdr = s[a + 2] == 1 ? 3 : 4;
		int type = s[a + hdr] & 0x1f;
		if (type == 1 || type == 5) {
			units.push_back(cur);
			cur.clear();
		}
	}
	return units;
}

static uint8_t Y(const MfH264Decoder::Frame& f, int x, int y) { return f.plane[0][y * f.pitch[0] + x]; }
static uint8_t U(const MfH264Decoder::Frame& f, int x, int y) { return f.plane[1][(y / 2) * f.pitch[1] + (x / 2) * 2]; }
static uint8_t V(const MfH264Decoder::Frame& f, int x, int y) { return f.plane[1][(y / 2) * f.pitch[1] + (x / 2) * 2 + 1]; }

TEST_CASE("an H.264 decoder is available on this machine")
{
	CHECK(MfH264Decoder::available());
}

TEST_CASE("decodes the test clip to NV12 with ffmpeg's pixel values")
{
	std::vector<uint8_t> clip = read_clip();
	REQUIRE(clip.size() > 1000);
	std::vector<std::vector<uint8_t>> units = access_units(clip);
	REQUIRE(units.size() == 10);

	MfH264Decoder dec;
	REQUIRE(dec.open());
	CHECK(dec.hardware());   // this machine has a GPU: decoding must go through D3D11

	int frames = 0;
	bool checked_first = false;
	auto take = [&](const MfH264Decoder::Frame& f) {
		CHECK(f.width == 320);
		CHECK(f.height == 240);
		if (!checked_first) {
			checked_first = true;
			CHECK(Y(f, 0, 0) == 16);
			CHECK(Y(f, 160, 120) == 210);
			CHECK(Y(f, 319, 239) == 235);
			CHECK(Y(f, 40, 200) == 165);
			CHECK(U(f, 160, 120) == 16);
			CHECK(V(f, 160, 120) == 146);
		}
		frames++;
	};
	for (size_t i = 0; i < units.size(); i++) {
		CHECK(dec.decode(units[i].data(), units[i].size(), i * 33333));
		while (dec.receive()) take(dec.frame());
	}
	while (dec.drain()) take(dec.frame());
	CHECK(frames == 10);
	CHECK(checked_first);
}

TEST_CASE("decodes in software when asked, with the same pixel values")
{
	std::vector<std::vector<uint8_t>> units = access_units(read_clip());
	REQUIRE(units.size() == 10);

	MfH264Decoder dec;
	REQUIRE(dec.open(NULL, false));
	CHECK_FALSE(dec.hardware());

	int frames = 0;
	bool checked_first = false;
	auto take = [&](const MfH264Decoder::Frame& f) {
		if (!checked_first) {
			checked_first = true;
			CHECK(f.texture == NULL);
			REQUIRE(f.plane[0] != NULL);
			CHECK(f.width == 320);
			CHECK(f.height == 240);
			CHECK(Y(f, 0, 0) == 16);
			CHECK(Y(f, 160, 120) == 210);
			CHECK(Y(f, 319, 239) == 235);
			CHECK(U(f, 160, 120) == 16);
			CHECK(V(f, 160, 120) == 146);
		}
		frames++;
	};
	for (size_t i = 0; i < units.size(); i++) {
		CHECK(dec.decode(units[i].data(), units[i].size(), i * 33333));
		while (dec.receive()) take(dec.frame());
	}
	while (dec.drain()) take(dec.frame());
	CHECK(frames == 10);
	CHECK(checked_first);
}

TEST_CASE("without low-latency mode every frame still comes out by the end")
{
	std::vector<std::vector<uint8_t>> units = access_units(read_clip());
	REQUIRE(units.size() == 10);
	MfH264Decoder dec;
	REQUIRE(dec.open(NULL, true, false));
	int frames = 0;
	for (size_t i = 0; i < units.size(); i++) {
		CHECK(dec.decode(units[i].data(), units[i].size(), i * 33333));
		while (dec.receive()) frames++;
	}
	while (dec.drain()) frames++;
	CHECK(frames == 10);
}

// Run by hand: LITEMIRROR_BENCH_CLIP=<annex b file> litemirror-tests.exe -tc=benchmark* -no-skip
TEST_CASE("benchmark: decode LITEMIRROR_BENCH_CLIP and report CPU time" * doctest::skip(true))
{
	const char* path = getenv("LITEMIRROR_BENCH_CLIP");
	REQUIRE(path != NULL);
	std::vector<uint8_t> clip;
	{
		FILE* f = fopen(path, "rb");
		REQUIRE(f != NULL);
		uint8_t tmp[65536];
		size_t n;
		while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) clip.insert(clip.end(), tmp, tmp + n);
		fclose(f);
	}
	std::vector<std::vector<uint8_t>> units = access_units(clip);

	MfH264Decoder dec;
	REQUIRE(dec.open());

	FILETIME c, e, k0, u0, k1, u1;
	LARGE_INTEGER freq, t0, t1;
	QueryPerformanceFrequency(&freq);
	GetProcessTimes(GetCurrentProcess(), &c, &e, &k0, &u0);
	QueryPerformanceCounter(&t0);
	int frames = 0;
	for (size_t i = 0; i < units.size(); i++) {
		dec.decode(units[i].data(), units[i].size(), i * 16667);
		while (dec.receive()) frames++;
	}
	while (dec.drain()) frames++;
	QueryPerformanceCounter(&t1);
	GetProcessTimes(GetCurrentProcess(), &c, &e, &k1, &u1);

	auto ft = [](const FILETIME& a, const FILETIME& b) {
		return (double)(((ULONGLONG)b.dwHighDateTime << 32 | b.dwLowDateTime) -
		                ((ULONGLONG)a.dwHighDateTime << 32 | a.dwLowDateTime)) / 1e7;
	};
	double wall = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
	double cpu = ft(k0, k1) + ft(u0, u1);
	MESSAGE("hardware=" << dec.hardware() << " frames=" << frames << " wall=" << wall << "s fps=" << frames / wall
	        << " cpu=" << cpu << "s (" << 100.0 * cpu / wall << "% of one core)");
	CHECK(frames == (int)units.size());
}

TEST_CASE("garbage input does not produce a frame or crash")
{
	MfH264Decoder dec;
	REQUIRE(dec.open());
	uint8_t junk[64];
	for (int i = 0; i < 64; i++) junk[i] = (uint8_t)(i * 37);
	dec.decode(junk, sizeof(junk), 0);   // the MFT may take it or refuse it
	CHECK_FALSE(dec.receive());
	CHECK_FALSE(dec.drain());
}
