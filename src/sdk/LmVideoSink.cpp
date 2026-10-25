// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include "LmVideoSink.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

template <class T> static void release(T*& p) { if (p) { p->Release(); p = NULL; } }

LmVideoSink::LmVideoSink()
	: m_hwnd(NULL), m_device(NULL), m_context(NULL), m_swap(NULL),
	  m_videoDevice(NULL), m_videoContext(NULL), m_enumerator(NULL), m_processor(NULL),
	  m_clientW(0), m_clientH(0), m_srcW(0), m_srcH(0)
{
}

LmVideoSink::~LmVideoSink()
{
	close();
}

bool LmVideoSink::open(HWND window)
{
	close();
	if (!window) {
		return false;
	}
	m_hwnd = window;
	static const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
	};
	HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
	                               D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
	                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &m_device, NULL, &m_context);
	if (FAILED(hr)) {
		close();
		return false;
	}
	// the decoder's worker threads and the receive thread share this context
	ID3D11Multithread* mt = NULL;
	if (SUCCEEDED(m_context->QueryInterface(IID_PPV_ARGS(&mt)))) {
		mt->SetMultithreadProtected(TRUE);
		release(mt);
	}

	RECT rc = {0};
	GetClientRect(window, &rc);
	DXGI_SWAP_CHAIN_DESC1 sd = {0};
	sd.Width = rc.right > 0 ? rc.right : 1;
	sd.Height = rc.bottom > 0 ? rc.bottom : 1;
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	IDXGIDevice* dxgiDevice = NULL;
	IDXGIAdapter* adapter = NULL;
	IDXGIFactory2* factory = NULL;
	bool ok = SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
	          SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
	          SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) &&
	          SUCCEEDED(factory->CreateSwapChainForHwnd(m_device, window, &sd, NULL, NULL, &m_swap));
	if (ok) {
		// the shell owns full-screen switching, not Alt+Enter inside DXGI
		factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
		ok = SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&m_videoDevice))) &&
		     SUCCEEDED(m_context->QueryInterface(IID_PPV_ARGS(&m_videoContext)));
	}
	release(factory);
	release(adapter);
	release(dxgiDevice);
	if (!ok) {
		close();
		return false;
	}
	m_clientW = sd.Width;
	m_clientH = sd.Height;
	return true;
}

void LmVideoSink::close()
{
	release(m_processor);
	release(m_enumerator);
	release(m_videoContext);
	release(m_videoDevice);
	release(m_swap);
	release(m_context);
	release(m_device);
	m_hwnd = NULL;
	m_clientW = m_clientH = m_srcW = m_srcH = 0;
}

// Keep the swap chain the size of the client area
bool LmVideoSink::fitWindow()
{
	RECT rc = {0};
	if (!GetClientRect(m_hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) {
		return false;
	}
	if ((uint32_t)rc.right == m_clientW && (uint32_t)rc.bottom == m_clientH) {
		return true;
	}
	if (FAILED(m_swap->ResizeBuffers(0, rc.right, rc.bottom, DXGI_FORMAT_UNKNOWN, 0))) {
		return false;
	}
	m_clientW = rc.right;
	m_clientH = rc.bottom;
	m_srcW = m_srcH = 0;   // the letterbox rectangle depends on the window size
	return true;
}

// A video processor for this picture size and window size: colour space,
// source area (the texture is coded size, 1088 rows for 1080p) and the
// letterboxed destination.
bool LmVideoSink::setupProcessor(uint32_t width, uint32_t height)
{
	if (m_processor && width == m_srcW && height == m_srcH) {
		return true;
	}
	release(m_processor);
	release(m_enumerator);
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd = {};
	cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	cd.InputWidth = width;
	cd.InputHeight = height;
	cd.OutputWidth = m_clientW;
	cd.OutputHeight = m_clientH;
	cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
	if (FAILED(m_videoDevice->CreateVideoProcessorEnumerator(&cd, &m_enumerator)) ||
	    FAILED(m_videoDevice->CreateVideoProcessor(m_enumerator, 0, &m_processor))) {
		return false;
	}

	// Studio-range YCbCr in, full-range RGB out. The stream carries no colour
	// description, so follow the usual rule: BT.709 for HD, BT.601 below.
	D3D11_VIDEO_PROCESSOR_COLOR_SPACE in = {};
	in.YCbCr_Matrix = height > 576;
	in.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
	m_videoContext->VideoProcessorSetStreamColorSpace(m_processor, 0, &in);
	D3D11_VIDEO_PROCESSOR_COLOR_SPACE out = {};
	out.RGB_Range = 0;
	m_videoContext->VideoProcessorSetOutputColorSpace(m_processor, &out);
	m_videoContext->VideoProcessorSetStreamFrameFormat(m_processor, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
	// no driver "enhancements" on the picture
	m_videoContext->VideoProcessorSetStreamAutoProcessingMode(m_processor, 0, FALSE);

	RECT src = { 0, 0, (LONG)width, (LONG)height };
	m_videoContext->VideoProcessorSetStreamSourceRect(m_processor, 0, TRUE, &src);
	RECT dst;
	if ((uint64_t)m_clientW * height > (uint64_t)m_clientH * width) {
		LONG w = (LONG)((uint64_t)m_clientH * width / height);
		dst.left = ((LONG)m_clientW - w) / 2;
		dst.top = 0;
		dst.right = dst.left + w;
		dst.bottom = m_clientH;
	} else {
		LONG h = (LONG)((uint64_t)m_clientW * height / width);
		dst.left = 0;
		dst.top = ((LONG)m_clientH - h) / 2;
		dst.right = m_clientW;
		dst.bottom = dst.top + h;
	}
	m_videoContext->VideoProcessorSetStreamDestRect(m_processor, 0, TRUE, &dst);
	RECT all = { 0, 0, (LONG)m_clientW, (LONG)m_clientH };
	m_videoContext->VideoProcessorSetOutputTargetRect(m_processor, TRUE, &all);
	D3D11_VIDEO_COLOR black = {};
	black.RGBA.A = 1.0f;
	m_videoContext->VideoProcessorSetOutputBackgroundColor(m_processor, FALSE, &black);

	m_srcW = width;
	m_srcH = height;
	return true;
}

bool LmVideoSink::draw(ID3D11Texture2D* picture, uint32_t subresource, uint32_t width, uint32_t height)
{
	if (!m_swap || !picture || width == 0 || height == 0) {
		return false;
	}
	if (!fitWindow() || !setupProcessor(width, height)) {
		return false;
	}
	// flip model: buffer 0 is a different texture after every present
	ID3D11Texture2D* back = NULL;
	if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
		return false;
	}
	D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od = {};
	od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC id = {};
	id.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
	id.Texture2D.ArraySlice = subresource;
	ID3D11VideoProcessorOutputView* target = NULL;
	ID3D11VideoProcessorInputView* source = NULL;
	bool ok = SUCCEEDED(m_videoDevice->CreateVideoProcessorOutputView(back, m_enumerator, &od, &target)) &&
	          SUCCEEDED(m_videoDevice->CreateVideoProcessorInputView(picture, m_enumerator, &id, &source));
	if (ok) {
		D3D11_VIDEO_PROCESSOR_STREAM stream = {};
		stream.Enable = TRUE;
		stream.pInputSurface = source;
		ok = SUCCEEDED(m_videoContext->VideoProcessorBlt(m_processor, target, 0, 1, &stream));
	}
	release(source);
	release(target);
	release(back);
	return ok;
}

void LmVideoSink::present()
{
	if (m_swap) {
		m_swap->Present(0, 0);   // no vsync wait: DWM composes, the picture shows as soon as it exists
	}
}
