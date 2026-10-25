// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors

#include <initguid.h>   // define the codec and CLSID GUIDs in this file
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <string.h>
#include "MfH264Decoder.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")   // CLSID_CMSH264DecoderMFT
#pragma comment(lib, "ole32.lib")

template <class T> static void release(T*& p) { if (p) { p->Release(); p = NULL; } }

static IMFTransform* createDecoder()
{
	IMFTransform* mft = NULL;
	if (FAILED(CoCreateInstance(CLSID_CMSH264DecoderMFT, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mft)))) {
		return NULL;
	}
	return mft;
}

MfH264Decoder::MfH264Decoder()
	: m_mft(NULL), m_outSample(NULL), m_locked(NULL), m_locked2d(NULL),
	  m_codedWidth(0), m_codedHeight(0), m_draining(false)
{
	memset(&m_frame, 0, sizeof(m_frame));
}

MfH264Decoder::~MfH264Decoder()
{
	close();
}

bool MfH264Decoder::available()
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	IMFTransform* mft = createDecoder();
	bool ok = mft != NULL;
	release(mft);
	return ok;
}

bool MfH264Decoder::open()
{
	close();
	CoInitializeEx(NULL, COINIT_MULTITHREADED);   // RPC_E_CHANGED_MODE is fine: COM is up
	if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
		return false;
	}
	m_mft = createDecoder();
	if (!m_mft) {
		return false;
	}

	// Output pictures as soon as they are decodable instead of holding a reorder queue
	ICodecAPI* api = NULL;
	if (SUCCEEDED(m_mft->QueryInterface(IID_PPV_ARGS(&api)))) {
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_UI4;
		v.ulVal = 1;
		api->SetValue(&CODECAPI_AVLowLatencyMode, &v);
		release(api);
	}

	IMFMediaType* in = NULL;
	bool ok = SUCCEEDED(MFCreateMediaType(&in)) &&
	          SUCCEEDED(in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
	          SUCCEEDED(in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) &&
	          SUCCEEDED(in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) &&
	          SUCCEEDED(m_mft->SetInputType(0, in, 0));
	release(in);
	if (!ok || !pickOutputType()) {
		close();
		return false;
	}
	m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
	return true;
}

// Choose NV12 among the output types the decoder offers and remember the
// coded and visible sizes (1080p streams are coded as 1088 rows).
bool MfH264Decoder::pickOutputType()
{
	IMFMediaType* type = NULL;
	for (DWORD i = 0; SUCCEEDED(m_mft->GetOutputAvailableType(0, i, &type)); i++) {
		GUID sub = {0};
		type->GetGUID(MF_MT_SUBTYPE, &sub);
		if (sub == MFVideoFormat_NV12 && SUCCEEDED(m_mft->SetOutputType(0, type, 0))) {
			UINT32 w = 0, h = 0;
			MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
			m_codedWidth = w;
			m_codedHeight = h;
			m_frame.width = w;
			m_frame.height = h;
			MFVideoArea area;
			if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), NULL))) {
				m_frame.width = area.Area.cx;
				m_frame.height = area.Area.cy;
			}
			release(type);
			return true;
		}
		release(type);
	}
	return false;
}

void MfH264Decoder::close()
{
	unlockFrame();
	release(m_outSample);
	if (m_mft) {
		release(m_mft);
		MFShutdown();
	}
	m_draining = false;
}

void MfH264Decoder::unlockFrame()
{
	if (m_locked2d) {
		m_locked2d->Unlock2D();
	} else if (m_locked) {
		m_locked->Unlock();
	}
	release(m_locked2d);
	release(m_locked);
	release(m_outSample);   // the decoder refuses a sample it has already filled once
	m_frame.plane[0] = m_frame.plane[1] = NULL;
}

bool MfH264Decoder::decode(const uint8_t* data, size_t size, uint64_t pts)
{
	if (!m_mft || !data || size == 0) {
		return false;
	}
	if (m_draining) {
		// a new access unit after a drain starts a fresh stream
		m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
		m_draining = false;
	}

	IMFSample* sample = NULL;
	IMFMediaBuffer* buffer = NULL;
	BYTE* dst = NULL;
	bool fed = SUCCEEDED(MFCreateSample(&sample)) &&
	           SUCCEEDED(MFCreateMemoryBuffer((DWORD)size, &buffer)) &&
	           SUCCEEDED(buffer->Lock(&dst, NULL, NULL));
	if (fed) {
		memcpy(dst, data, size);
		buffer->Unlock();
		buffer->SetCurrentLength((DWORD)size);
		sample->AddBuffer(buffer);
		sample->SetSampleTime((LONGLONG)pts * 10);   // 100 ns units
		fed = SUCCEEDED(m_mft->ProcessInput(0, sample, 0));
	}
	release(buffer);
	release(sample);
	return fed;
}

bool MfH264Decoder::receive()
{
	return m_mft && output();
}

bool MfH264Decoder::drain()
{
	if (!m_mft) {
		return false;
	}
	if (!m_draining) {
		m_draining = true;
		m_mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
	}
	return output();
}

// One ProcessOutput call. Fills m_frame and keeps the buffer locked.
bool MfH264Decoder::output()
{
	unlockFrame();
	MFT_OUTPUT_STREAM_INFO info = {0};
	m_mft->GetOutputStreamInfo(0, &info);
	bool weAllocate = !(info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));

	for (int attempt = 0; attempt < 2; attempt++) {
		MFT_OUTPUT_DATA_BUFFER out = {0};
		if (weAllocate) {
			if (!m_outSample) {
				IMFMediaBuffer* buf = NULL;
				if (FAILED(MFCreateSample(&m_outSample)) ||
				    FAILED(MFCreateAlignedMemoryBuffer(info.cbSize, info.cbAlignment ? info.cbAlignment - 1 : 15, &buf))) {
					release(m_outSample);
					return false;
				}
				m_outSample->AddBuffer(buf);
				release(buf);
			}
			out.pSample = m_outSample;
		}
		DWORD status = 0;
		HRESULT hr = m_mft->ProcessOutput(0, 1, &out, &status);
		if (out.pEvents) out.pEvents->Release();

		if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
			// new resolution: renegotiate and retry once; our buffer may be too small now
			release(m_outSample);
			if (!pickOutputType()) return false;
			m_mft->GetOutputStreamInfo(0, &info);
			continue;
		}
		if (FAILED(hr)) {
			if (!weAllocate && out.pSample) out.pSample->Release();
			return false;   // MF_E_TRANSFORM_NEED_MORE_INPUT or a real error
		}

		IMFSample* sample = out.pSample;
		if (!sample) return false;
		IMFMediaBuffer* buffer = NULL;
		if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
			if (!weAllocate) sample->Release();
			return false;
		}
		BYTE* base = NULL;
		LONG pitch = 0;
		if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&m_locked2d))) &&
		    SUCCEEDED(m_locked2d->Lock2D(&base, &pitch))) {
			// locked through the 2D interface
		} else {
			release(m_locked2d);
			if (FAILED(buffer->Lock(&base, NULL, NULL))) {
				release(buffer);
				if (!weAllocate) sample->Release();
				return false;
			}
			pitch = (LONG)m_codedWidth;
		}
		m_locked = buffer;   // released by unlockFrame()
		m_frame.plane[0] = base;
		m_frame.plane[1] = base + (size_t)pitch * m_codedHeight;
		m_frame.pitch[0] = m_frame.pitch[1] = (uint32_t)pitch;
		LONGLONG t = 0;
		sample->GetSampleTime(&t);
		m_frame.pts = (uint64_t)t / 10;
		m_frame.key = MFGetAttributeUINT32(sample, MFSampleExtension_CleanPoint, 0) != 0;
		if (!weAllocate) sample->Release();   // the buffer keeps the memory alive
		return true;
	}
	return false;
}
