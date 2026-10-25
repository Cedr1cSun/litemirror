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
	void present();

private:
	bool fitWindow();
	bool setupProcessor(uint32_t width, uint32_t height);

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
};
