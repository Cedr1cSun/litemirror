// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// H.264 decoder on top of Windows Media Foundation's built-in decoder MFT.
// Takes Annex B access units (SPS/PPS in-band, as the mirror stream sends
// them) and hands back NV12 frames. No third-party code. When a D3D11
// device can be created the decoding runs on the GPU and the picture is
// read back once; otherwise the MFT decodes in software.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct IMFTransform;
struct IMFSample;
struct IMFMediaBuffer;
struct IMF2DBuffer;
struct IMFDXGIDeviceManager;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

class MfH264Decoder
{
public:
	struct Frame {
		uint32_t width, height;      // visible size
		const uint8_t* plane[2];     // Y, interleaved UV
		uint32_t pitch[2];
		uint64_t pts;                // microseconds, as given to decode()
		bool key;
		// Set instead of plane[] when the decoder was opened on a caller's
		// device: the picture is array slice `subresource` of `texture`.
		ID3D11Texture2D* texture;
		uint32_t subresource;
	};

	MfH264Decoder();
	~MfH264Decoder();

	// Can an H.264 decoder be created on this machine? False on Windows N
	// without the Media Feature Pack.
	static bool available();

	// With a device, pictures stay on the GPU as textures on that device
	// (frame().texture); without one they are read back into memory.
	// hardware=false decodes on the CPU (frame().plane[]), for machines
	// whose GPU decoder misbehaves. lowLatency=false lets the decoder keep
	// its reorder queue, which costs a few frames of delay.
	bool open(ID3D11Device* device = NULL, bool hardware = true, bool lowLatency = true);
	void close();

	// Feed one access unit; true when the decoder took it. Call receive()
	// until it returns false after every decode(): the MFT refuses new input
	// while pictures are waiting to be collected.
	bool decode(const uint8_t* data, size_t size, uint64_t pts);
	// True when frame() holds a new picture; it stays valid until the next
	// receive(), drain() or close().
	bool receive();
	// Flush remaining pictures at end of stream; true while one came out.
	bool drain();
	const Frame& frame() const { return m_frame; }
	// True when the decoder runs on the GPU through D3D11
	bool hardware() const { return m_hardware; }

private:
	bool setupD3D11(ID3D11Device* shared);
	bool pickOutputType();
	bool output();
	bool readback(IMFSample* sample);
	bool hold(IMFSample* sample);
	void unlockFrame();

	IMFTransform* m_mft;
	IMFSample* m_outSample;      // our own output sample when the MFT does not allocate
	IMFMediaBuffer* m_locked;
	IMF2DBuffer* m_locked2d;
	uint32_t m_codedWidth, m_codedHeight;
	bool m_draining;
	bool m_hardware;
	bool m_shared;   // pictures go out as textures, not memory
	Frame m_frame;

	// D3D11 path
	ID3D11Device* m_device;
	ID3D11DeviceContext* m_context;
	IMFDXGIDeviceManager* m_manager;
	ID3D11Texture2D* m_staging;   // CPU-readable copy of the current picture
	bool m_mapped;
};
