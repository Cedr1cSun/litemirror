// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Plays the receiver's PCM on the default output device through WASAPI in
// shared mode. No thread of its own: each packet is written straight into
// the engine buffer from the receiver's audio thread, and playback starts
// once a little is queued so network jitter does not cause gaps.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "litemirror.h"

struct IAudioClient;
struct IAudioRenderClient;
struct ISimpleAudioVolume;

class LmAudioSink
{
public:
	LmAudioSink();
	~LmAudioSink();

	struct Device {
		std::wstring id;     // IMMDevice id, stable across reboots
		std::wstring name;   // what Windows shows
	};
	// Active output devices on this machine
	static std::vector<Device> devices();
	// Play on this device instead of the default one; "" = default.
	// Takes effect when the device is (re)opened.
	void setDevice(const std::wstring& id);
	// The phone's volume, -30..0 dB (-144 = mute), onto this app's audio
	// session, 0..1 in step with the phone's slider
	void setVolume(float db);
	// The session's volume (0..1), or what setVolume last asked for while no device is open
	float volume() const;

	// Queue one packet. Opens the device on first use and again after a
	// format change or when the device goes away; silently drops audio
	// while there is no device.
	void play(const lm_audio_frame* frame);
	// Drop what is queued (the phone paused or seeked)
	void flush();
	void close();

private:
	bool open(uint32_t rate, uint16_t channels);

	IAudioClient* m_client;
	IAudioRenderClient* m_render;
	uint32_t m_bufferFrames;
	uint32_t m_rate;
	uint16_t m_channels;
	bool m_started;
	ULONGLONG m_retryAt;   // no device: do not hammer the enumerator
	std::wstring m_deviceId;
	ISimpleAudioVolume* m_volume;
	float m_level;         // 0..1, applied to each device as it opens
	bool m_levelSet;
};
