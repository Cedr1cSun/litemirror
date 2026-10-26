// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// LmVideoSink puts decoded pictures on screen through a D3D11 swap chain.
// Decode the first frame of tests/unit/data/clip.h264 on the sink's device,
// draw it into a hidden window and read the back buffer: the picture must
// sit centred between black bars and its colours must match what ffmpeg
// makes of the same frame (BT.601, a couple of levels of tolerance for the
// GPU's conversion).
#include "doctest.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "MfH264Decoder.h"
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

// SPS, PPS and the IDR slice: the stream up to the start code after the IDR
static std::vector<uint8_t> first_access_unit(const std::vector<uint8_t>& s)
{
	bool after_idr = false;
	for (size_t i = 0; i + 3 < s.size(); i++) {
		if (!(s[i] == 0 && s[i + 1] == 0 && (s[i + 2] == 1 || (s[i + 2] == 0 && s[i + 3] == 1)))) continue;
		if (after_idr) return std::vector<uint8_t>(s.begin(), s.begin() + i);
		after_idr = (s[i + (s[i + 2] == 1 ? 3 : 4)] & 0x1f) == 5;
		i += 2;
	}
	return s;
}

struct Pixels {
	std::vector<uint8_t> bgra;
	UINT width = 0, height = 0, pitch = 0;
	// largest channel difference between the pixel at x,y and r,g,b
	int diff(int x, int y, int r, int g, int b) const {
		const uint8_t* p = &bgra[(size_t)y * pitch + (size_t)x * 4];
		int d = abs(p[2] - r);
		if (abs(p[1] - g) > d) d = abs(p[1] - g);
		if (abs(p[0] - b) > d) d = abs(p[0] - b);
		return d;
	}
};

// Copy the back buffer the sink has just drawn into CPU memory
static bool read_back(LmVideoSink& sink, Pixels& out)
{
	ID3D11Texture2D* back = NULL;
	if (FAILED(sink.swapChain()->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
	D3D11_TEXTURE2D_DESC desc;
	back->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.MiscFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* staging = NULL;
	ID3D11DeviceContext* ctx = NULL;
	sink.device()->GetImmediateContext(&ctx);
	bool ok = SUCCEEDED(sink.device()->CreateTexture2D(&desc, NULL, &staging));
	D3D11_MAPPED_SUBRESOURCE map = {0};
	if (ok) {
		ctx->CopyResource(staging, back);
		ok = SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map));
	}
	if (ok) {
		out.width = desc.Width;
		out.height = desc.Height;
		out.pitch = map.RowPitch;
		out.bgra.assign((const uint8_t*)map.pData, (const uint8_t*)map.pData + (size_t)map.RowPitch * desc.Height);
		ctx->Unmap(staging, 0);
	}
	if (staging) staging->Release();
	ctx->Release();
	back->Release();
	return ok;
}

TEST_CASE("sink draws the decoded picture letterboxed with ffmpeg's colours")
{
	std::vector<uint8_t> au = first_access_unit(read_clip());
	REQUIRE(au.size() > 100);
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 640, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);

	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	MfH264Decoder dec;
	REQUIRE(dec.open(sink.device()));   // pictures land in textures the sink can draw directly
	REQUIRE(dec.decode(au.data(), au.size(), 0));
	REQUIRE(dec.receive());
	const MfH264Decoder::Frame& f = dec.frame();
	REQUIRE(f.texture != NULL);
	CHECK(f.plane[0] == NULL);
	CHECK(f.width == 320);
	CHECK(f.height == 240);

	// 640x240 window, 320x240 picture: unscaled in the middle, black bars left and right
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	Pixels px;
	REQUIRE(read_back(sink, px));
	CHECK(px.width == 640);
	CHECK(px.height == 240);
	CHECK(px.diff(10, 120, 0, 0, 0) == 0);
	CHECK(px.diff(630, 120, 0, 0, 0) == 0);
	// points inside flat colour bars: at edges the GPU's chroma interpolation legitimately differs
	CHECK(px.diff(160 + 8, 8, 0, 0, 0) <= 3);
	CHECK(px.diff(160 + 296, 8, 255, 255, 255) <= 3);
	CHECK(px.diff(160 + 168, 8, 253, 253, 0) <= 3);
	CHECK(px.diff(160 + 136, 8, 0, 0, 254) <= 3);
	CHECK(px.diff(160 + 256, 44, 253, 0, 0) <= 3);
	CHECK(px.diff(160 + 216, 16, 0, 255, 0) <= 3);
	CHECK(px.diff(160 + 104, 8, 254, 0, 253) <= 3);
	CHECK(px.diff(160 + 40, 200, 251, 166, 0) <= 3);
	sink.present();

	// portrait window: the swap chain follows the client size, the bars move to top and bottom
	SetWindowPos(hwnd, NULL, 0, 0, 320, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, px));
	CHECK(px.width == 320);
	CHECK(px.height == 480);
	CHECK(px.diff(160, 10, 0, 0, 0) == 0);
	CHECK(px.diff(160, 470, 0, 0, 0) == 0);
	CHECK(px.diff(168, 120 + 8, 253, 253, 0) <= 3);
	CHECK(px.diff(136, 120 + 8, 0, 0, 254) <= 3);
	sink.present();

	dec.close();
	sink.close();
	DestroyWindow(hwnd);
}

TEST_CASE("sink draws a picture handed over in memory with the same colours")
{
	std::vector<uint8_t> au = first_access_unit(read_clip());
	REQUIRE(au.size() > 100);
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 640, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);

	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	MfH264Decoder dec;
	REQUIRE(dec.open(NULL, false));   // software: the picture arrives as NV12 planes in memory
	REQUIRE(dec.decode(au.data(), au.size(), 0));
	REQUIRE(dec.receive());
	const MfH264Decoder::Frame& f = dec.frame();
	REQUIRE(f.plane[0] != NULL);
	CHECK(f.texture == NULL);

	REQUIRE(sink.draw(f.plane[0], f.pitch[0], f.plane[1], f.pitch[1], f.width, f.height));
	Pixels px;
	REQUIRE(read_back(sink, px));
	CHECK(px.diff(10, 120, 0, 0, 0) == 0);
	CHECK(px.diff(630, 120, 0, 0, 0) == 0);
	CHECK(px.diff(160 + 8, 8, 0, 0, 0) <= 3);
	CHECK(px.diff(160 + 296, 8, 255, 255, 255) <= 3);
	CHECK(px.diff(160 + 168, 8, 253, 253, 0) <= 3);
	CHECK(px.diff(160 + 136, 8, 0, 0, 254) <= 3);
	CHECK(px.diff(160 + 256, 44, 253, 0, 0) <= 3);
	CHECK(px.diff(160 + 216, 16, 0, 255, 0) <= 3);
	CHECK(px.diff(160 + 104, 8, 254, 0, 253) <= 3);
	CHECK(px.diff(160 + 40, 200, 251, 166, 0) <= 3);
	sink.present();

	dec.close();
	sink.close();
	DestroyWindow(hwnd);
}

// Decode the first frame on the sink's device, ready to draw
struct FirstFrame {
	MfH264Decoder dec;
	const MfH264Decoder::Frame* f = NULL;
	bool open(LmVideoSink& sink) {
		std::vector<uint8_t> au = first_access_unit(read_clip());
		if (au.size() < 100 || !dec.open(sink.device()) || !dec.decode(au.data(), au.size(), 0) || !dec.receive()) return false;
		f = &dec.frame();
		return f->texture != NULL;
	}
};

TEST_CASE("colour filters adjust the picture and reset puts it back")
{
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 320, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	FirstFrame first;
	REQUIRE(first.open(sink));
	const MfH264Decoder::Frame& f = *first.f;
	Pixels px;

	// no saturation: the red bar turns grey
	LmColour c = {};
	c.saturation = -1.0f;
	sink.setColour(c);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, px));
	if (!sink.supportsFilters()) {
		MESSAGE("this GPU has no video processor filters; skipped");
	} else {
		const uint8_t* p = &px.bgra[(size_t)44 * px.pitch + 256 * 4];
		CHECK(abs(p[2] - p[1]) <= 12);
		CHECK(abs(p[1] - p[0]) <= 12);

		// brightness up: the black bar is no longer black
		c.saturation = 0;
		c.brightness = 0.5f;
		sink.setColour(c);
		REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
		REQUIRE(read_back(sink, px));
		CHECK(px.diff(8, 8, 0, 0, 0) > 20);
	}

	// everything back to neutral: ffmpeg's colours again
	c = LmColour();
	sink.setColour(c);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, px));
	CHECK(px.diff(8, 8, 0, 0, 0) <= 3);
	CHECK(px.diff(256, 44, 253, 0, 0) <= 3);
	CHECK(px.diff(168, 8, 253, 253, 0) <= 3);
	sink.close();
	DestroyWindow(hwnd);
}

TEST_CASE("colour matrix and range change the conversion")
{
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 320, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	FirstFrame first;
	REQUIRE(first.open(sink));
	const MfH264Decoder::Frame& f = *first.f;
	Pixels px;

	// full range in: Y=16 is a dark grey, not black
	LmColour c = {};
	c.range = 2;
	sink.setColour(c);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, px));
	CHECK(px.diff(8, 8, 0, 0, 0) >= 10);
	CHECK(px.diff(8, 8, 16, 16, 16) <= 4);

	// BT.709 for this SD clip: the yellow bar's green changes against BT.601
	c.range = 0;
	c.matrix = 2;
	sink.setColour(c);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, px));
	CHECK(px.diff(168, 8, 253, 253, 0) > 3);
	c.matrix = 1;   // explicit BT.601 is what auto picks for 240 rows
	sink.setColour(c);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, px));
	CHECK(px.diff(168, 8, 253, 253, 0) <= 3);
	sink.close();
	DestroyWindow(hwnd);
}

TEST_CASE("sink presents with vsync when low latency is off")
{
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 320, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	sink.setLowLatency(false);
	FirstFrame first;
	REQUIRE(first.open(sink));
	const MfH264Decoder::Frame& f = *first.f;
	UINT before = 0, after = 0;
	sink.swapChain()->GetLastPresentCount(&before);
	for (int i = 0; i < 3; i++) {
		REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
		sink.present();
	}
	sink.swapChain()->GetLastPresentCount(&after);
	CHECK(after - before == 3);
	sink.close();
	DestroyWindow(hwnd);
}

// DXGI queues up to three presented frames ahead of the display by default,
// which is up to three frames of delay nobody asked for. With low latency on
// the sink allows one; off, the default is back.
TEST_CASE("sink lets DXGI queue one frame when low latency is on")
{
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 320, 240, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	IDXGIDevice1* dxgi = NULL;
	REQUIRE(SUCCEEDED(sink.device()->QueryInterface(IID_PPV_ARGS(&dxgi))));
	UINT queued = 0;
	dxgi->GetMaximumFrameLatency(&queued);
	CHECK(queued == 1);

	FirstFrame first;
	REQUIRE(first.open(sink));
	const MfH264Decoder::Frame& f = *first.f;
	UINT before = 0, after = 0;
	sink.swapChain()->GetLastPresentCount(&before);
	for (int i = 0; i < 3; i++) {
		REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
		sink.present();
	}
	sink.swapChain()->GetLastPresentCount(&after);
	CHECK(after - before == 3);   // every present went through

	sink.setLowLatency(false);
	sink.present();
	dxgi->GetMaximumFrameLatency(&queued);
	CHECK(queued == 3);
	dxgi->Release();
	sink.close();
	DestroyWindow(hwnd);
}

TEST_CASE("high-quality scaler keeps the colours and differs from the video processor at edges")
{
	// a 640x480 window shows the 320x240 picture at twice its size
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 640, 480, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	LmVideoSink sink;
	REQUIRE(sink.open(hwnd));
	FirstFrame first;
	REQUIRE(first.open(sink));
	const MfH264Decoder::Frame& f = *first.f;

	Pixels plain, sharp;
	sink.setScaler(0);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, plain));
	sink.setScaler(1);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, sharp));
	CHECK(sharp.width == 640);
	CHECK(sharp.height == 480);

	// flat colour bars, at twice the source coordinates: the conversion is the same as before
	CHECK(sharp.diff(16, 16, 0, 0, 0) <= 3);
	CHECK(sharp.diff(592, 16, 255, 255, 255) <= 3);
	CHECK(sharp.diff(336, 16, 253, 253, 0) <= 3);
	CHECK(sharp.diff(272, 16, 0, 0, 254) <= 3);
	CHECK(sharp.diff(512, 88, 253, 0, 0) <= 3);
	CHECK(sharp.diff(432, 32, 0, 255, 0) <= 3);
	CHECK(sharp.diff(208, 16, 254, 0, 253) <= 3);
	CHECK(sharp.diff(80, 400, 251, 166, 0) <= 3);

	// the shader path was really taken: along a row that crosses the bar edges the two differ
	int differing = 0;
	for (int x = 0; x < 640; x++) {
		const uint8_t* a = &plain.bgra[(size_t)16 * plain.pitch + (size_t)x * 4];
		const uint8_t* b = &sharp.bgra[(size_t)16 * sharp.pitch + (size_t)x * 4];
		if (abs(a[0] - b[0]) > 3 || abs(a[1] - b[1]) > 3 || abs(a[2] - b[2]) > 3) differing++;
	}
	CHECK(differing > 0);
	CHECK(differing < 64);   // only near the edges, not everywhere
	sink.present();

	// a window smaller than the picture: no upscaling, the processor path stays and draws
	SetWindowPos(hwnd, NULL, 0, 0, 160, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	REQUIRE(sink.draw(f.texture, f.subresource, f.width, f.height));
	REQUIRE(read_back(sink, sharp));
	CHECK(sharp.width == 160);
	CHECK(sharp.diff(148, 4, 255, 255, 255) <= 6);
	sink.present();
	sink.close();
	DestroyWindow(hwnd);
}

TEST_CASE("sink refuses to open without a window")
{
	LmVideoSink sink;
	CHECK_FALSE(sink.open(NULL));
	CHECK(sink.device() == NULL);
}
