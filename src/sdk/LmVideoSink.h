// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Puts decoded pictures on screen: a flip-model DXGI swap chain on the
// caller's window and ID3D11VideoProcessor to turn NV12 into RGB and scale
// it on the GPU. The decoder renders into textures on this sink's device,
// so a frame goes from decoder to screen without a copy through memory.
#pragma once
#include <windows.h>
#include <stdint.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IDXGISwapChain1;
struct ID3D11VideoDevice;
struct ID3D11VideoContext;
struct ID3D11VideoProcessorEnumerator;
struct ID3D11VideoProcessor;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

// Picture adjustments, see lm_colour in litemirror.h
struct LmColour {
	LmColour() : matrix(0), range(0), brightness(0), contrast(0), saturation(0), hue(0) {}
	int matrix;        // 0 auto (BT.709 for HD, BT.601 below), 1 BT.601, 2 BT.709
	int range;         // 0 auto (studio), 1 studio 16-235, 2 full 0-255
	float brightness, contrast, saturation, hue;   // -1..1, 0 = as decoded
};

class LmVideoSink
{
public:
	LmVideoSink();
	~LmVideoSink();

	// Fails without Direct3D 11 hardware (no GPU driver, some remote sessions)
	bool open(HWND window);
	void close();

	// Decoders must create their pictures on this device; valid while open
	ID3D11Device* device() const { return m_device; }
	IDXGISwapChain1* swapChain() const { return m_swap; }

	// Convert and scale one NV12 picture (array slice `subresource` of
	// `picture`, visible size width x height) into the back buffer, centred
	// between black bars so the aspect ratio holds. Follows window resizes.
	// False while the window has no client area (minimised).
	bool draw(ID3D11Texture2D* picture, uint32_t subresource, uint32_t width, uint32_t height);
	// The same for an NV12 picture in memory (software decoding): the
	// planes are copied into a texture first.
	bool draw(const uint8_t* y, uint32_t yPitch, const uint8_t* uv, uint32_t uvPitch, uint32_t width, uint32_t height);
	void present();

	// May be called from any thread while frames are being drawn; the
	// next draw() uses it
	void setColour(const LmColour& colour);
	// Whether this GPU's video processor has brightness, contrast,
	// saturation and hue filters (valid after the first draw)
	bool supportsFilters() const { return m_filters; }
	// On (default): present at once. Off: wait for the next vertical
	// blank, smoother but up to a frame later.
	void setLowLatency(bool on) { m_lowLatency = on; }
	// 0: the video processor scales (bilinear on most drivers). 1: when the
	// window is larger than the picture, the processor only converts to RGB
	// and a Catmull-Rom bicubic shader does the enlarging. May be changed
	// at any time; the next draw() uses it.
	void setScaler(int quality) { m_scaler = quality; }
	// Super resolution while enlarging: 0 off, 1 AMD FSR 1.0 (two shader
	// passes, feature level 11), 2 NVIDIA RTX VSR, 3 Intel VSR (extensions on
	// the video processor; other GPUs and old drivers scale as usual). 1 to 3
	// take precedence over setScaler(). The next draw() uses it.
	void setSuperResolution(int mode) { m_super = mode; }
	// Whether a hardware GPU of that PCI vendor is in this machine
	static bool hasVendor(unsigned vendor);

private:
	bool fitWindow();
	void applyLatency();
	bool setupProcessor(uint32_t width, uint32_t height, bool toTexture);
	void applyColour(uint32_t height);
	static RECT letterbox(uint32_t width, uint32_t height, uint32_t outW, uint32_t outH);
	bool setupShader();
	bool setupRgb(uint32_t width, uint32_t height);
	bool blit(ID3D11Texture2D* picture, uint32_t subresource, ID3D11Texture2D* target);
	void pass(ID3D11RenderTargetView* rtv, const RECT& area, ID3D11PixelShader* ps, ID3D11ShaderResourceView* srv, ID3D11Buffer* constants);
	void enlarge(ID3D11Texture2D* back, uint32_t width, uint32_t height);
	bool setupFsr();
	bool setupMid(uint32_t width, uint32_t height);
	void fsr(ID3D11Texture2D* back, uint32_t width, uint32_t height);
	void vendorSuper();

	HWND m_hwnd;
	ID3D11Device* m_device;
	ID3D11DeviceContext* m_context;
	IDXGISwapChain1* m_swap;
	ID3D11VideoDevice* m_videoDevice;
	ID3D11VideoContext* m_videoContext;
	ID3D11VideoProcessorEnumerator* m_enumerator;
	ID3D11VideoProcessor* m_processor;
	uint32_t m_clientW, m_clientH;   // swap chain size
	uint32_t m_srcW, m_srcH;         // picture size the processor was set up for
	bool m_toTexture;                // ... and whether its output was the RGB texture, not the window
	// the high-quality scaler: picture -> RGB texture -> shader -> window
	int m_scaler;
	ID3D11VertexShader* m_vs;
	ID3D11PixelShader* m_ps;
	ID3D11SamplerState* m_sampler;
	ID3D11Buffer* m_constants;
	ID3D11Texture2D* m_rgb;
	ID3D11ShaderResourceView* m_rgbView;
	uint32_t m_rgbW, m_rgbH;
	// super resolution: FSR's two shaders, its constants and the picture between the passes
	int m_super;
	int m_processorSuper;            // the driver mode the processor was set up with
	unsigned m_vendor;               // PCI vendor of the device's adapter
	ID3D11PixelShader* m_easu;
	ID3D11PixelShader* m_rcas;
	ID3D11Buffer* m_fsrConstants;
	ID3D11Texture2D* m_mid;
	ID3D11RenderTargetView* m_midRtv;
	ID3D11ShaderResourceView* m_midSrv;
	uint32_t m_midW, m_midH;
	ID3D11Texture2D* m_staging;      // CPU-written copy of a software-decoded picture
	ID3D11Texture2D* m_upload;       // its GPU copy the processor reads
	uint32_t m_uploadW, m_uploadH;
	CRITICAL_SECTION m_lock;         // guards m_colour and m_colourDirty
	LmColour m_colour;
	bool m_colourDirty;
	bool m_filters;
	bool m_lowLatency;
	int m_maxLatency;                // frames DXGI may queue, as last told to the device
	bool m_tearing;                  // the swap chain was created with ALLOW_TEARING
};
