// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// LmAudioSink plays the receiver's PCM on the default output device through
// WASAPI. Feed it a 1 kHz tone the way the receiver does (480-sample
// packets, in real time) while a loopback capture listens on the same
// device: the capture must contain that tone. Skipped when the machine has
// no output device.
#include "doctest.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <math.h>
#include <string>
#include <vector>
#include "LmAudioSink.h"

template <class T> static void release(T*& p) { if (p) { p->Release(); p = NULL; } }

// Records what the default output device plays, as float samples of channel 0
struct Loopback {
	IAudioClient* client = NULL;
	IAudioCaptureClient* capture = NULL;
	WAVEFORMATEX* format = NULL;
	std::vector<float> samples;

	bool open() {
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		IMMDeviceEnumerator* devices = NULL;
		IMMDevice* device = NULL;
		bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
		          SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
		          SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client)) &&
		          SUCCEEDED(client->GetMixFormat(&format)) &&
		          SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 20000000, 0, format, NULL)) &&
		          SUCCEEDED(client->GetService(IID_PPV_ARGS(&capture))) &&
		          SUCCEEDED(client->Start());
		release(device);
		release(devices);
		return ok && is_float();
	}
	bool is_float() const {
		if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
		if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE) return false;
		return ((WAVEFORMATEXTENSIBLE*)format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	}
	void pull() {
		UINT32 frames = 0;
		while (SUCCEEDED(capture->GetNextPacketSize(&frames)) && frames > 0) {
			BYTE* data = NULL;
			DWORD flags = 0;
			if (FAILED(capture->GetBuffer(&data, &frames, &flags, NULL, NULL))) return;
			for (UINT32 i = 0; i < frames; i++)
				samples.push_back((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : ((const float*)data)[i * format->nChannels]);
			capture->ReleaseBuffer(frames);
		}
	}
	~Loopback() {
		if (client) client->Stop();
		release(capture);
		release(client);
		if (format) CoTaskMemFree(format);
	}
};

// Frequency of the dominant tone in the window by counting zero crossings
static double tone_hz(const std::vector<float>& s, size_t from, size_t to, double rate)
{
	int crossings = 0;
	for (size_t i = from + 1; i < to; i++)
		if ((s[i - 1] < 0) != (s[i] < 0)) crossings++;
	return crossings / 2.0 * rate / (double)(to - from);
}

TEST_CASE("audio sink plays the receiver's PCM on the default output device")
{
	Loopback listen;
	if (!listen.open()) {
		MESSAGE("no output device with a float mix format; skipped");
		return;
	}

	const uint32_t rate = 44100;
	const uint16_t channels = 2;
	const int per_packet = 480;   // AAC-ELD frame, as the mirror stream sends them
	std::vector<int16_t> pcm(per_packet * channels);
	LmAudioSink sink;

	double phase = 0;
	for (int n = 0; n < 60; n++) {   // ~650 ms of a 1 kHz tone, paced like the real stream
		for (int i = 0; i < per_packet; i++) {
			int16_t v = (int16_t)(12000 * sin(phase));
			pcm[i * 2] = pcm[i * 2 + 1] = v;
			phase += 2 * 3.14159265358979 * 1000 / rate;
		}
		lm_audio_frame f;
		f.pts = n * per_packet;
		f.sample_rate = rate;
		f.channels = channels;
		f.bits_per_sample = 16;
		f.data = (const uint8_t*)pcm.data();
		f.size = pcm.size() * sizeof(int16_t);
		sink.play(&f);
		listen.pull();
		Sleep(per_packet * 1000 / rate);
	}
	Sleep(100);
	listen.pull();
	sink.close();

	double mixRate = listen.format->nSamplesPerSec;
	REQUIRE(listen.samples.size() > (size_t)(mixRate / 2));
	// judge the 200 ms before the sound stopped: the tone must be there and be 1 kHz
	size_t to = listen.samples.size();
	while (to > 0 && fabsf(listen.samples[to - 1]) < 0.01f) to--;
	REQUIRE(to > (size_t)(mixRate / 5));
	size_t from = to - (size_t)(mixRate / 5);
	float peak = 0;
	for (size_t i = from; i < to; i++) if (fabsf(listen.samples[i]) > peak) peak = fabsf(listen.samples[i]);
	CHECK(peak > 0.05f);
	double hz = tone_hz(listen.samples, from, to, mixRate);
	CHECK(hz > 900);
	CHECK(hz < 1100);
}

// The id of the default output device, as the enumerator spells it
static std::wstring default_device_id()
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	IMMDeviceEnumerator* devices = NULL;
	IMMDevice* device = NULL;
	LPWSTR id = NULL;
	std::wstring out;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
	    SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
	    SUCCEEDED(device->GetId(&id))) {
		out = id;
		CoTaskMemFree(id);
	}
	release(device);
	release(devices);
	return out;
}

TEST_CASE("audio sink lists the output devices and plays on the one picked")
{
	Loopback listen;
	if (!listen.open()) {
		MESSAGE("no output device with a float mix format; skipped");
		return;
	}
	std::vector<LmAudioSink::Device> list = LmAudioSink::devices();
	REQUIRE(list.size() >= 1);
	std::wstring wanted = default_device_id();
	bool listed = false;
	for (size_t i = 0; i < list.size(); i++) {
		CHECK(!list[i].id.empty());
		CHECK(!list[i].name.empty());
		if (list[i].id == wanted) listed = true;
	}
	CHECK(listed);   // the default device is among them

	// pick the default device by id and the loopback on it hears the tone
	LmAudioSink sink;
	sink.setDevice(wanted);
	const uint32_t rate = 44100;
	std::vector<int16_t> pcm(480 * 2);
	double phase = 0;
	for (int n = 0; n < 40; n++) {
		for (int i = 0; i < 480; i++) {
			pcm[i * 2] = pcm[i * 2 + 1] = (int16_t)(12000 * sin(phase));
			phase += 2 * 3.14159265358979 * 1000 / rate;
		}
		lm_audio_frame f = { (uint64_t)n * 480, rate, 2, 16, (const uint8_t*)pcm.data(), pcm.size() * sizeof(int16_t) };
		sink.play(&f);
		listen.pull();
		Sleep(480 * 1000 / rate);
	}
	Sleep(100);
	listen.pull();
	sink.close();
	float peak = 0;
	for (size_t i = 0; i < listen.samples.size(); i++) if (fabsf(listen.samples[i]) > peak) peak = fabsf(listen.samples[i]);
	CHECK(peak > 0.05f);
}

TEST_CASE("audio sink stays quiet but alive on an unknown device id")
{
	LmAudioSink sink;
	sink.setDevice(L"{0.0.0.00000000}.{00000000-0000-0000-0000-000000000000}");
	std::vector<int16_t> pcm(480 * 2, 0);
	lm_audio_frame f = { 0, 44100, 2, 16, (const uint8_t*)pcm.data(), pcm.size() * sizeof(int16_t) };
	sink.play(&f);
	sink.play(&f);
	sink.close();
}

TEST_CASE("audio sink volume follows the phone's dB scale")
{
	LmAudioSink sink;
	sink.setVolume(-15.0f);           // half way up the phone's slider
	CHECK(sink.volume() == doctest::Approx(0.5f).epsilon(0.02));
	std::vector<int16_t> pcm(480 * 2, 0);
	lm_audio_frame f = { 0, 44100, 2, 16, (const uint8_t*)pcm.data(), pcm.size() * sizeof(int16_t) };
	sink.play(&f);                    // opens the device: the volume must land on the session
	CHECK(sink.volume() == doctest::Approx(0.5f).epsilon(0.02));
	sink.setVolume(0.0f);
	CHECK(sink.volume() == doctest::Approx(1.0f).epsilon(0.02));
	sink.setVolume(-30.0f);
	CHECK(sink.volume() == doctest::Approx(0.0f).epsilon(0.02));
	sink.setVolume(-144.0f);          // mute
	CHECK(sink.volume() == doctest::Approx(0.0f).epsilon(0.02));
	sink.close();
}

TEST_CASE("audio sink survives a format change and empty frames")
{
	LmAudioSink sink;
	std::vector<int16_t> pcm(480 * 2, 0);
	lm_audio_frame f;
	f.pts = 0;
	f.sample_rate = 44100;
	f.channels = 2;
	f.bits_per_sample = 16;
	f.data = (const uint8_t*)pcm.data();
	f.size = pcm.size() * sizeof(int16_t);
	sink.play(&f);
	f.sample_rate = 48000;
	f.channels = 1;
	sink.play(&f);
	f.size = 0;
	sink.play(&f);
	sink.flush();
	sink.close();
}
