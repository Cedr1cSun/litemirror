// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors

#include <initguid.h>   // define the codec and CLSID GUIDs in this file
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
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
#pragma comment(lib, "oleaut32.lib")   // VariantInit
#pragma comment(lib, "d3d11.lib")

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
	  m_codedWidth(0), m_codedHeight(0), m_draining(false), m_hardware(false), m_shared(false),
	  m_device(NULL), m_context(NULL), m_manager(NULL), m_staging(NULL), m_mapped(false)
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

// A D3D11 device for the decoder: the caller's, or our own. Creating one
// fails without a GPU (or over some remote sessions); the caller then
// stays on the software path.
bool MfH264Decoder::setupD3D11(ID3D11Device* shared)
{
	if (shared) {
		m_device = shared;
		m_device->AddRef();
		m_device->GetImmediateContext(&m_context);
	} else {
		static const D3D_FEATURE_LEVEL levels[] = {
			D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
		};
		HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
		                               D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &m_device, NULL, &m_context);
		if (FAILED(hr)) {
			return false;
		}
	}
	// the decoder uses the device from its own threads
	ID3D11Multithread* mt = NULL;
	if (SUCCEEDED(m_context->QueryInterface(IID_PPV_ARGS(&mt)))) {
		mt->SetMultithreadProtected(TRUE);
		release(mt);
	}
	UINT token = 0;
	if (FAILED(MFCreateDXGIDeviceManager(&token, &m_manager)) ||
	    FAILED(m_manager->ResetDevice(m_device, token))) {
		return false;
	}
	return true;
}

bool MfH264Decoder::open(ID3D11Device* device, bool hardware)
{
	close();
	m_shared = hardware && device != NULL;
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

	// GPU decoding when the MFT and the machine allow it
	UINT32 aware = 0;
	IMFAttributes* attrs = NULL;
	if (SUCCEEDED(m_mft->GetAttributes(&attrs))) {
		attrs->GetUINT32(MF_SA_D3D11_AWARE, &aware);
		release(attrs);
	}
	if (hardware && aware && setupD3D11(device)) {
		m_hardware = SUCCEEDED(m_mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)m_manager));
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
	release(m_staging);
	if (m_mft) {
		release(m_mft);
		MFShutdown();
	}
	release(m_manager);
	release(m_context);
	release(m_device);
	m_draining = false;
	m_hardware = false;
	m_shared = false;
}

void MfH264Decoder::unlockFrame()
{
	if (m_mapped) {
		m_context->Unmap(m_staging, 0);
		m_mapped = false;
	}
	if (m_locked2d) {
		m_locked2d->Unlock2D();
	} else if (m_locked) {
		m_locked->Unlock();
	}
	release(m_locked2d);
	release(m_locked);
	release(m_frame.texture);
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

// GPU picture -> staging texture -> mapped CPU memory. One copy, done by
// the GPU; the map stays until the next picture.
bool MfH264Decoder::readback(IMFSample* sample)
{
	IMFMediaBuffer* buffer = NULL;
	IMFDXGIBuffer* dxgi = NULL;
	ID3D11Texture2D* tex = NULL;
	UINT sub = 0;
	bool ok = SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) &&
	          SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
	          SUCCEEDED(dxgi->GetResource(IID_PPV_ARGS(&tex))) &&
	          SUCCEEDED(dxgi->GetSubresourceIndex(&sub));
	D3D11_TEXTURE2D_DESC desc = {0};
	if (ok) {
		tex->GetDesc(&desc);
		if (m_staging) {
			D3D11_TEXTURE2D_DESC have;
			m_staging->GetDesc(&have);
			if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format) {
				release(m_staging);
			}
		}
		if (!m_staging) {
			D3D11_TEXTURE2D_DESC sd = desc;
			sd.ArraySize = 1;
			sd.MipLevels = 1;
			sd.Usage = D3D11_USAGE_STAGING;
			sd.BindFlags = 0;
			sd.MiscFlags = 0;
			sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ok = SUCCEEDED(m_device->CreateTexture2D(&sd, NULL, &m_staging));
		}
	}
	D3D11_MAPPED_SUBRESOURCE map = {0};
	if (ok) {
		m_context->CopySubresourceRegion(m_staging, 0, 0, 0, 0, tex, sub, NULL);
		ok = SUCCEEDED(m_context->Map(m_staging, 0, D3D11_MAP_READ, 0, &map));
	}
	release(tex);
	release(dxgi);
	release(buffer);
	if (!ok) {
		return false;
	}
	m_mapped = true;
	m_frame.plane[0] = (const uint8_t*)map.pData;
	m_frame.plane[1] = m_frame.plane[0] + (size_t)map.RowPitch * desc.Height;
	m_frame.pitch[0] = m_frame.pitch[1] = map.RowPitch;
	return true;
}

// Shared device: hand out the decoder's texture itself. The sample stays
// alive (m_outSample) until the next picture so the texture is not reused.
bool MfH264Decoder::hold(IMFSample* sample)
{
	IMFMediaBuffer* buffer = NULL;
	IMFDXGIBuffer* dxgi = NULL;
	UINT sub = 0;
	bool ok = SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) &&
	          SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
	          SUCCEEDED(dxgi->GetResource(IID_PPV_ARGS(&m_frame.texture))) &&
	          SUCCEEDED(dxgi->GetSubresourceIndex(&sub));
	release(dxgi);
	release(buffer);
	if (!ok) {
		release(m_frame.texture);
		return false;
	}
	m_frame.subresource = sub;
	return true;
}

// One ProcessOutput call. Fills m_frame and keeps the picture locked.
bool MfH264Decoder::output()
{
	unlockFrame();
	MFT_OUTPUT_STREAM_INFO info = {0};
	m_mft->GetOutputStreamInfo(0, &info);
	bool weAllocate = !(info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));

	for (int attempt = 0; attempt < 2; attempt++) {
		MFT_OUTPUT_DATA_BUFFER out = {0};
		if (weAllocate) {
			IMFMediaBuffer* buf = NULL;
			if (FAILED(MFCreateSample(&m_outSample)) ||
			    FAILED(MFCreateAlignedMemoryBuffer(info.cbSize, info.cbAlignment ? info.cbAlignment - 1 : 15, &buf))) {
				release(m_outSample);
				return false;
			}
			m_outSample->AddBuffer(buf);
			release(buf);
			out.pSample = m_outSample;
		}
		DWORD status = 0;
		HRESULT hr = m_mft->ProcessOutput(0, 1, &out, &status);
		if (out.pEvents) out.pEvents->Release();

		if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
			// new resolution: renegotiate and retry once
			release(m_outSample);
			release(m_staging);
			if (!pickOutputType()) return false;
			m_mft->GetOutputStreamInfo(0, &info);
			continue;
		}
		if (FAILED(hr)) {
			release(m_outSample);
			if (!weAllocate && out.pSample) out.pSample->Release();
			return false;   // MF_E_TRANSFORM_NEED_MORE_INPUT or a real error
		}

		IMFSample* sample = out.pSample;
		if (!sample) return false;
		bool got;
		if (m_hardware && m_shared) {
			got = hold(sample);
			if (got && !weAllocate) m_outSample = sample;
		} else if (m_hardware) {
			got = readback(sample);
		} else {
			IMFMediaBuffer* buffer = NULL;
			BYTE* base = NULL;
			LONG pitch = 0;
			got = SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer));
			if (got) {
				if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&m_locked2d))) &&
				    SUCCEEDED(m_locked2d->Lock2D(&base, &pitch))) {
					// locked through the 2D interface
				} else {
					release(m_locked2d);
					got = SUCCEEDED(buffer->Lock(&base, NULL, NULL));
					pitch = (LONG)m_codedWidth;
				}
			}
			if (got) {
				m_locked = buffer;   // released by unlockFrame()
				m_frame.plane[0] = base;
				m_frame.plane[1] = base + (size_t)pitch * m_codedHeight;
				m_frame.pitch[0] = m_frame.pitch[1] = (uint32_t)pitch;
			} else {
				release(buffer);
			}
		}
		if (got) {
			LONGLONG t = 0;
			sample->GetSampleTime(&t);
			m_frame.pts = (uint64_t)t / 10;
			m_frame.key = MFGetAttributeUINT32(sample, MFSampleExtension_CleanPoint, 0) != 0;
		}
		if (!weAllocate && m_outSample != sample) sample->Release();   // the staging copy or locked buffer keeps the pixels
		return got;
	}
	return false;
}
