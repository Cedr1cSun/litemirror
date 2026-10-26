// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors

#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <string.h>
#include "LmVideoSink.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Catmull-Rom bicubic enlarging: 16 taps round the source position. The
// vertex shader draws one triangle over the viewport from the vertex id.
static const char SCALER_HLSL[] =
"cbuffer C : register(b0) { float4 size; }\n"   // width, height, 1/width, 1/height
"Texture2D tex : register(t0);\n"
"SamplerState smp : register(s0);\n"
"struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
"V vs(uint id : SV_VertexID) {\n"
"  V o; float2 p = float2((id << 1) & 2, id & 2);\n"
"  o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
"float w(float x) { x = abs(x);\n"
"  return x < 1 ? 1.5 * x * x * x - 2.5 * x * x + 1 : x < 2 ? -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2 : 0; }\n"
"float4 ps(V i) : SV_Target {\n"
"  float2 p = i.uv * size.xy - 0.5; float2 f = frac(p); float2 b = floor(p);\n"
"  float4 c = 0; float total = 0;\n"
"  [unroll] for (int y = -1; y <= 2; y++) { float wy = w(y - f.y);\n"
"    [unroll] for (int x = -1; x <= 2; x++) { float k = w(x - f.x) * wy;\n"
"      c += tex.SampleLevel(smp, (b + float2(x, y) + 0.5) * size.zw, 0) * k; total += k; } }\n"
"  return saturate(c / total); }\n";

template <class T> static void release(T*& p) { if (p) { p->Release(); p = NULL; } }

LmVideoSink::LmVideoSink()
	: m_hwnd(NULL), m_device(NULL), m_context(NULL), m_swap(NULL),
	  m_videoDevice(NULL), m_videoContext(NULL), m_enumerator(NULL), m_processor(NULL),
	  m_clientW(0), m_clientH(0), m_srcW(0), m_srcH(0),
	  m_staging(NULL), m_upload(NULL), m_uploadW(0), m_uploadH(0),
	  m_colourDirty(false), m_filters(false), m_lowLatency(true), m_maxLatency(0),
	  m_toTexture(false), m_scaler(0), m_vs(NULL), m_ps(NULL), m_sampler(NULL), m_constants(NULL),
	  m_rgb(NULL), m_rgbView(NULL), m_rgbW(0), m_rgbH(0)
{
	InitializeCriticalSection(&m_lock);
}

LmVideoSink::~LmVideoSink()
{
	close();
	DeleteCriticalSection(&m_lock);
}

void LmVideoSink::setColour(const LmColour& colour)
{
	EnterCriticalSection(&m_lock);
	m_colour = colour;
	m_colourDirty = true;
	LeaveCriticalSection(&m_lock);
}

// Colour space and filters on the processor's stream, from the current settings
void LmVideoSink::applyColour(uint32_t height)
{
	EnterCriticalSection(&m_lock);
	LmColour c = m_colour;
	m_colourDirty = false;
	LeaveCriticalSection(&m_lock);

	// Studio-range YCbCr in, full-range RGB out, unless told otherwise. The
	// stream carries no colour description, so auto follows the usual rule:
	// BT.709 for HD, BT.601 below.
	D3D11_VIDEO_PROCESSOR_COLOR_SPACE in = {};
	in.YCbCr_Matrix = c.matrix == 0 ? height > 576 : c.matrix == 2;
	in.Nominal_Range = c.range == 2 ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255 : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
	m_videoContext->VideoProcessorSetStreamColorSpace(m_processor, 0, &in);
	D3D11_VIDEO_PROCESSOR_COLOR_SPACE out = {};
	out.RGB_Range = 0;
	m_videoContext->VideoProcessorSetOutputColorSpace(m_processor, &out);

	D3D11_VIDEO_PROCESSOR_CAPS caps = {};
	m_enumerator->GetVideoProcessorCaps(&caps);
	static const struct { D3D11_VIDEO_PROCESSOR_FILTER filter; UINT cap; } filters[] = {
		{ D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS, D3D11_VIDEO_PROCESSOR_FILTER_CAPS_BRIGHTNESS },
		{ D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST,   D3D11_VIDEO_PROCESSOR_FILTER_CAPS_CONTRAST },
		{ D3D11_VIDEO_PROCESSOR_FILTER_SATURATION, D3D11_VIDEO_PROCESSOR_FILTER_CAPS_SATURATION },
		{ D3D11_VIDEO_PROCESSOR_FILTER_HUE,        D3D11_VIDEO_PROCESSOR_FILTER_CAPS_HUE },
	};
	const float values[] = { c.brightness, c.contrast, c.saturation, c.hue };
	m_filters = true;
	for (int i = 0; i < 4; i++) {
		if (!(caps.FilterCaps & filters[i].cap)) {
			m_filters = false;
			continue;
		}
		// -1..1 maps onto the driver's own range either side of its default
		D3D11_VIDEO_PROCESSOR_FILTER_RANGE range = {};
		m_enumerator->GetVideoProcessorFilterRange(filters[i].filter, &range);
		float f = values[i];
		int level = range.Default + (int)(f * (f >= 0 ? range.Maximum - range.Default : range.Default - range.Minimum) + (f >= 0 ? 0.5f : -0.5f));
		m_videoContext->VideoProcessorSetStreamFilter(m_processor, 0, filters[i].filter, f != 0, level);
	}
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
	m_maxLatency = 0;
	applyLatency();
	return true;
}

// DXGI lets three presented frames queue ahead of the display by default,
// each one a frame of delay. One is enough for a live picture.
void LmVideoSink::applyLatency()
{
	int latency = m_lowLatency ? 1 : 3;
	if (latency == m_maxLatency) {
		return;
	}
	IDXGIDevice1* dxgi = NULL;
	if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
		dxgi->SetMaximumFrameLatency(latency);
		release(dxgi);
	}
	m_maxLatency = latency;
}

void LmVideoSink::close()
{
	release(m_rgbView);
	release(m_rgb);
	release(m_constants);
	release(m_sampler);
	release(m_ps);
	release(m_vs);
	m_rgbW = m_rgbH = 0;
	release(m_staging);
	release(m_upload);
	m_uploadW = m_uploadH = 0;
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
bool LmVideoSink::setupProcessor(uint32_t width, uint32_t height, bool toTexture)
{
	if (m_processor && width == m_srcW && height == m_srcH && toTexture == m_toTexture) {
		return true;
	}
	release(m_processor);
	release(m_enumerator);
	uint32_t outW = toTexture ? width : m_clientW, outH = toTexture ? height : m_clientH;
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd = {};
	cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	cd.InputWidth = width;
	cd.InputHeight = height;
	cd.OutputWidth = outW;
	cd.OutputHeight = outH;
	cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
	if (FAILED(m_videoDevice->CreateVideoProcessorEnumerator(&cd, &m_enumerator)) ||
	    FAILED(m_videoDevice->CreateVideoProcessor(m_enumerator, 0, &m_processor))) {
		return false;
	}

	applyColour(height);
	m_videoContext->VideoProcessorSetStreamFrameFormat(m_processor, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
	// no driver "enhancements" on the picture
	m_videoContext->VideoProcessorSetStreamAutoProcessingMode(m_processor, 0, FALSE);

	RECT src = { 0, 0, (LONG)width, (LONG)height };
	m_videoContext->VideoProcessorSetStreamSourceRect(m_processor, 0, TRUE, &src);
	RECT dst = letterbox(width, height, outW, outH);
	m_videoContext->VideoProcessorSetStreamDestRect(m_processor, 0, TRUE, &dst);
	RECT all = { 0, 0, (LONG)outW, (LONG)outH };
	m_videoContext->VideoProcessorSetOutputTargetRect(m_processor, TRUE, &all);
	D3D11_VIDEO_COLOR black = {};
	black.RGBA.A = 1.0f;
	m_videoContext->VideoProcessorSetOutputBackgroundColor(m_processor, FALSE, &black);

	m_srcW = width;
	m_srcH = height;
	m_toTexture = toTexture;
	return true;
}

// Where a width x height picture goes inside an outW x outH area, aspect kept, centred
RECT LmVideoSink::letterbox(uint32_t width, uint32_t height, uint32_t outW, uint32_t outH)
{
	RECT dst;
	if ((uint64_t)outW * height > (uint64_t)outH * width) {
		LONG w = (LONG)((uint64_t)outH * width / height);
		dst.left = ((LONG)outW - w) / 2;
		dst.top = 0;
		dst.right = dst.left + w;
		dst.bottom = outH;
	} else {
		LONG h = (LONG)((uint64_t)outW * height / width);
		dst.left = 0;
		dst.top = ((LONG)outH - h) / 2;
		dst.right = outW;
		dst.bottom = dst.top + h;
	}
	return dst;
}

// Compile the scaler once per device
bool LmVideoSink::setupShader()
{
	if (m_ps) {
		return true;
	}
	ID3DBlob* vs = NULL;
	ID3DBlob* ps = NULL;
	bool ok = SUCCEEDED(D3DCompile(SCALER_HLSL, sizeof(SCALER_HLSL) - 1, "scaler", NULL, NULL, "vs", "vs_4_0", 0, 0, &vs, NULL)) &&
	          SUCCEEDED(D3DCompile(SCALER_HLSL, sizeof(SCALER_HLSL) - 1, "scaler", NULL, NULL, "ps", "ps_4_0", 0, 0, &ps, NULL)) &&
	          SUCCEEDED(m_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), NULL, &m_vs)) &&
	          SUCCEEDED(m_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), NULL, &m_ps));
	release(ps);
	release(vs);
	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = 16;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	ok = ok && SUCCEEDED(m_device->CreateSamplerState(&sd, &m_sampler)) &&
	     SUCCEEDED(m_device->CreateBuffer(&bd, NULL, &m_constants));
	if (!ok) {
		release(m_constants);
		release(m_sampler);
		release(m_ps);
		release(m_vs);
	}
	return ok;
}

// The RGB picture at its own size, between the processor and the shader
bool LmVideoSink::setupRgb(uint32_t width, uint32_t height)
{
	if (m_rgb && width == m_rgbW && height == m_rgbH) {
		return true;
	}
	release(m_rgbView);
	release(m_rgb);
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = width;
	td.Height = height;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(m_device->CreateTexture2D(&td, NULL, &m_rgb)) ||
	    FAILED(m_device->CreateShaderResourceView(m_rgb, NULL, &m_rgbView))) {
		release(m_rgbView);
		release(m_rgb);
		return false;
	}
	m_rgbW = width;
	m_rgbH = height;
	return true;
}

// One VideoProcessorBlt of the picture into target
bool LmVideoSink::blit(ID3D11Texture2D* picture, uint32_t subresource, ID3D11Texture2D* target)
{
	D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od = {};
	od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC id = {};
	id.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
	id.Texture2D.ArraySlice = subresource;
	ID3D11VideoProcessorOutputView* out = NULL;
	ID3D11VideoProcessorInputView* in = NULL;
	bool ok = SUCCEEDED(m_videoDevice->CreateVideoProcessorOutputView(target, m_enumerator, &od, &out)) &&
	          SUCCEEDED(m_videoDevice->CreateVideoProcessorInputView(picture, m_enumerator, &id, &in));
	if (ok) {
		D3D11_VIDEO_PROCESSOR_STREAM stream = {};
		stream.Enable = TRUE;
		stream.pInputSurface = in;
		ok = SUCCEEDED(m_videoContext->VideoProcessorBlt(m_processor, out, 0, 1, &stream));
	}
	release(in);
	release(out);
	return ok;
}

// The shader pass: the RGB picture enlarged into the letterbox area of the back buffer
void LmVideoSink::enlarge(ID3D11Texture2D* back, uint32_t width, uint32_t height)
{
	ID3D11RenderTargetView* rtv = NULL;
	if (FAILED(m_device->CreateRenderTargetView(back, NULL, &rtv))) {
		return;
	}
	D3D11_MAPPED_SUBRESOURCE map = {};
	if (SUCCEEDED(m_context->Map(m_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
		float* size = (float*)map.pData;
		size[0] = (float)width;
		size[1] = (float)height;
		size[2] = 1.0f / width;
		size[3] = 1.0f / height;
		m_context->Unmap(m_constants, 0);
	}
	const float black[4] = { 0, 0, 0, 1 };
	m_context->ClearRenderTargetView(rtv, black);
	RECT dst = letterbox(width, height, m_clientW, m_clientH);
	D3D11_VIEWPORT vp = {};
	vp.TopLeftX = (float)dst.left;
	vp.TopLeftY = (float)dst.top;
	vp.Width = (float)(dst.right - dst.left);
	vp.Height = (float)(dst.bottom - dst.top);
	vp.MaxDepth = 1.0f;
	m_context->OMSetRenderTargets(1, &rtv, NULL);
	m_context->RSSetViewports(1, &vp);
	m_context->IASetInputLayout(NULL);
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_context->VSSetShader(m_vs, NULL, 0);
	m_context->PSSetShader(m_ps, NULL, 0);
	m_context->PSSetConstantBuffers(0, 1, &m_constants);
	m_context->PSSetShaderResources(0, 1, &m_rgbView);
	m_context->PSSetSamplers(0, 1, &m_sampler);
	m_context->Draw(3, 0);
	ID3D11ShaderResourceView* none = NULL;
	m_context->PSSetShaderResources(0, 1, &none);
	ID3D11RenderTargetView* noTarget = NULL;
	m_context->OMSetRenderTargets(1, &noTarget, NULL);
	release(rtv);
}

bool LmVideoSink::draw(ID3D11Texture2D* picture, uint32_t subresource, uint32_t width, uint32_t height)
{
	if (!m_swap || !picture || width == 0 || height == 0) {
		return false;
	}
	if (!fitWindow()) {
		return false;
	}
	// the shader only enlarges; a picture bigger than the window is left to the processor
	bool sharp = m_scaler == 1 && (m_clientW > width || m_clientH > height) && setupShader() && setupRgb(width, height);
	if (!setupProcessor(width, height, sharp)) {
		return false;
	}
	if (m_colourDirty) {
		applyColour(height);   // the user moved a slider
	}
	// flip model: buffer 0 is a different texture after every present
	ID3D11Texture2D* back = NULL;
	if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
		return false;
	}
	bool ok = blit(picture, subresource, sharp ? m_rgb : back);
	if (ok && sharp) {
		enlarge(back, width, height);
	}
	release(back);
	return ok;
}

// Software-decoded picture: copy the planes into a staging texture, let the
// GPU move it into a decoder-style texture, then draw that as usual.
bool LmVideoSink::draw(const uint8_t* y, uint32_t yPitch, const uint8_t* uv, uint32_t uvPitch, uint32_t width, uint32_t height)
{
	if (!m_device || !y || !uv || width == 0 || height == 0) {
		return false;
	}
	uint32_t w = (width + 1) & ~1u, h = (height + 1) & ~1u;   // NV12 textures have even sizes
	if (m_upload && (w != m_uploadW || h != m_uploadH)) {
		release(m_staging);
		release(m_upload);
	}
	if (!m_upload) {
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = w;
		td.Height = h;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_NV12;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
		D3D11_TEXTURE2D_DESC sd = td;
		sd.Usage = D3D11_USAGE_STAGING;
		sd.BindFlags = 0;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(m_device->CreateTexture2D(&td, NULL, &m_upload)) ||
		    FAILED(m_device->CreateTexture2D(&sd, NULL, &m_staging))) {
			release(m_staging);
			release(m_upload);
			return false;
		}
		m_uploadW = w;
		m_uploadH = h;
	}
	D3D11_MAPPED_SUBRESOURCE map = {};
	if (FAILED(m_context->Map(m_staging, 0, D3D11_MAP_WRITE, 0, &map))) {
		return false;
	}
	uint8_t* dst = (uint8_t*)map.pData;
	for (uint32_t r = 0; r < height; r++) {
		memcpy(dst + (size_t)r * map.RowPitch, y + (size_t)r * yPitch, width);
	}
	dst += (size_t)map.RowPitch * h;
	for (uint32_t r = 0; r < (height + 1) / 2; r++) {
		memcpy(dst + (size_t)r * map.RowPitch, uv + (size_t)r * uvPitch, width);
	}
	m_context->Unmap(m_staging, 0);
	m_context->CopyResource(m_upload, m_staging);
	return draw(m_upload, 0, width, height);
}

void LmVideoSink::present()
{
	if (m_swap) {
		applyLatency();
		// interval 0: no vsync wait, DWM composes and the picture shows as soon as it exists
		m_swap->Present(m_lowLatency ? 0 : 1, 0);
	}
}
