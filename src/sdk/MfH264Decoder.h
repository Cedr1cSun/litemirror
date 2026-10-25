// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// H.264 decoder on top of Windows Media Foundation's built-in decoder MFT.
// Takes Annex B access units (SPS/PPS in-band, as the mirror stream sends
// them) and hands back NV12 frames. No third-party code, hardware assisted
// when a D3D11 device is attached (see setDevice).
#pragma once
#include <stddef.h>
#include <stdint.h>

struct IMFTransform;
struct IMFSample;
struct IMFMediaBuffer;
struct IMF2DBuffer;

class MfH264Decoder
{
public:
	struct Frame {
		uint32_t width, height;      // visible size
		const uint8_t* plane[2];     // Y, interleaved UV
		uint32_t pitch[2];
		uint64_t pts;                // microseconds, as given to decode()
		bool key;
	};

	MfH264Decoder();
	~MfH264Decoder();

	// Can an H.264 decoder be created on this machine? False on Windows N
	// without the Media Feature Pack.
	static bool available();

	bool open();
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

private:
	bool pickOutputType();
	bool output();
	void unlockFrame();

	IMFTransform* m_mft;
	IMFSample* m_outSample;      // our own output sample when the MFT does not allocate
	IMFMediaBuffer* m_locked;
	IMF2DBuffer* m_locked2d;
	uint32_t m_codedWidth, m_codedHeight;
	bool m_draining;
	Frame m_frame;
};
