// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string.h>
#include "LmAudioSink.h"

#pragma comment(lib, "ole32.lib")

template <class T> static void release(T*& p) { if (p) { p->Release(); p = NULL; } }

static const REFERENCE_TIME BUFFER_100NS = 3000000;   // 300 ms of room for network jitter
static const uint32_t PREROLL_MS = 50;

LmAudioSink::LmAudioSink()
	: m_client(NULL), m_render(NULL), m_bufferFrames(0), m_rate(0), m_channels(0), m_started(false), m_retryAt(0),
	  m_volume(NULL), m_level(1.0f), m_levelSet(false)
{
}

void LmAudioSink::setVolume(float db)
{
	// the phone's slider runs -30..0 dB in equal steps; keep it linear so the two sliders agree
	float level = db <= -144.0f ? 0.0f : (db + 30.0f) / 30.0f;
	m_level = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
	m_levelSet = true;
	if (m_volume) {
		m_volume->SetMasterVolume(m_level, NULL);
	}
}

float LmAudioSink::volume() const
{
	float level = m_level;
	if (m_volume) {
		m_volume->GetMasterVolume(&level);
	}
	return level;
}

LmAudioSink::~LmAudioSink()
{
	close();
}

std::vector<LmAudioSink::Device> LmAudioSink::devices()
{
	std::vector<Device> out;
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	IMMDeviceEnumerator* devices = NULL;
	IMMDeviceCollection* all = NULL;
	UINT count = 0;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
	    SUCCEEDED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &all)) &&
	    SUCCEEDED(all->GetCount(&count))) {
		for (UINT i = 0; i < count; i++) {
			IMMDevice* device = NULL;
			IPropertyStore* props = NULL;
			LPWSTR id = NULL;
			PROPVARIANT name;
			PropVariantInit(&name);
			if (SUCCEEDED(all->Item(i, &device)) && SUCCEEDED(device->GetId(&id)) &&
			    SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) &&
			    SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
				Device d;
				d.id = id;
				d.name = name.pwszVal;
				out.push_back(d);
			}
			PropVariantClear(&name);
			if (id) CoTaskMemFree(id);
			release(props);
			release(device);
		}
	}
	release(all);
	release(devices);
	return out;
}

void LmAudioSink::setDevice(const std::wstring& id)
{
	m_deviceId = id;
}

bool LmAudioSink::open(uint32_t rate, uint16_t channels)
{
	close();
	CoInitializeEx(NULL, COINIT_MULTITHREADED);   // the receiver's thread may not have COM yet
	WAVEFORMATEX fmt = {0};
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = channels;
	fmt.nSamplesPerSec = rate;
	fmt.wBitsPerSample = 16;
	fmt.nBlockAlign = (WORD)(channels * 2);
	fmt.nAvgBytesPerSec = rate * fmt.nBlockAlign;

	IMMDeviceEnumerator* devices = NULL;
	IMMDevice* device = NULL;
	// AUTOCONVERTPCM: the engine resamples to its own mix format (Windows 10 and later)
	bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
	          SUCCEEDED(m_deviceId.empty() ? devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)
	                                       : devices->GetDevice(m_deviceId.c_str(), &device)) &&
	          SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_client)) &&
	          SUCCEEDED(m_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
	                                         AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
	                                         BUFFER_100NS, 0, &fmt, NULL)) &&
	          SUCCEEDED(m_client->GetBufferSize(&m_bufferFrames)) &&
	          SUCCEEDED(m_client->GetService(IID_PPV_ARGS(&m_render)));
	release(device);
	release(devices);
	if (!ok) {
		close();
		return false;
	}
	if (SUCCEEDED(m_client->GetService(IID_PPV_ARGS(&m_volume))) && m_levelSet) {
		m_volume->SetMasterVolume(m_level, NULL);
	}
	m_rate = rate;
	m_channels = channels;
	return true;
}

void LmAudioSink::close()
{
	if (m_client) {
		m_client->Stop();
	}
	release(m_volume);
	release(m_render);
	release(m_client);
	m_rate = 0;
	m_channels = 0;
	m_started = false;
}

void LmAudioSink::flush()
{
	if (m_client) {
		m_client->Stop();
		m_client->Reset();
		m_started = false;
	}
}

void LmAudioSink::play(const lm_audio_frame* f)
{
	if (!f || !f->data || f->size == 0 || f->channels == 0 || f->bits_per_sample != 16) {
		return;
	}
	if (!m_client || f->sample_rate != m_rate || f->channels != m_channels) {
		if (GetTickCount64() < m_retryAt) {
			return;
		}
		if (!open(f->sample_rate, f->channels)) {
			m_retryAt = GetTickCount64() + 1000;
			return;
		}
	}
	UINT32 queued = 0;
	if (FAILED(m_client->GetCurrentPadding(&queued))) {
		close();   // the device went away; the next packet opens the new default
		return;
	}
	if (m_started && queued == 0) {
		m_client->Stop();   // ran dry: build up the pre-roll again before continuing
		m_started = false;
	}
	UINT32 frames = (UINT32)(f->size / (f->channels * 2));
	if (frames > m_bufferFrames - queued) {
		frames = m_bufferFrames - queued;   // further ahead than the buffer holds: drop the tail
	}
	BYTE* dst = NULL;
	if (frames == 0 || FAILED(m_render->GetBuffer(frames, &dst))) {
		return;
	}
	memcpy(dst, f->data, (size_t)frames * f->channels * 2);
	m_render->ReleaseBuffer(frames, 0);
	if (!m_started && (queued + frames) * 1000 >= PREROLL_MS * m_rate) {
		m_started = SUCCEEDED(m_client->Start());
	}
}
