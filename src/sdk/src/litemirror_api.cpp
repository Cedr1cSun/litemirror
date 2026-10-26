// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// The C API: argument checks and lifetime; FgAirplayServer does the work.

#include <new>
#include <string.h>
#include <string>
#include <vector>
#include "litemirror.h"
#include "FgAirplayServer.h"
#include "MfH264Decoder.h"
#include "LmVideoSink.h"
#include "LmAudioSink.h"

struct lm_server {
	explicit lm_server(const lm_callbacks& callbacks) : cb(callbacks), impl(NULL), sink(NULL), audio(NULL) {}
	~lm_server() { delete sink; delete audio; }
	lm_callbacks cb;
	FgAirplayServer* impl;   // non-NULL while running
	LmVideoSink* sink;       // non-NULL while a window is set
	LmAudioSink* audio;      // non-NULL while built-in playback is on
	FgServerOptions options; // everything else set before start
};

lm_error lm_server_set_address(lm_server* s, const char* ipv4)
{
	if (!s) return LM_E_INVALID_ARG;
	unsigned long address = 0;
	if (ipv4 && ipv4[0] && !FgAirplayServer::parseAddress(ipv4, &address)) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.address = address;
	return LM_OK;
}

static bool unit(float f) { return f >= -1.0f && f <= 1.0f; }

lm_error lm_server_set_colour(lm_server* s, const lm_colour* c)
{
	if (!s || !c || c->matrix < 0 || c->matrix > 2 || c->range < 0 || c->range > 2 ||
	    !unit(c->brightness) || !unit(c->contrast) || !unit(c->saturation) || !unit(c->hue)) {
		return LM_E_INVALID_ARG;
	}
	s->options.colour.matrix = c->matrix;
	s->options.colour.range = c->range;
	s->options.colour.brightness = c->brightness;
	s->options.colour.contrast = c->contrast;
	s->options.colour.saturation = c->saturation;
	s->options.colour.hue = c->hue;
	if (s->sink) s->sink->setColour(s->options.colour);
	return LM_OK;
}

lm_error lm_server_set_scaler(lm_server* s, int quality)
{
	if (!s || quality < 0 || quality > 1) return LM_E_INVALID_ARG;
	s->options.scaler = quality;
	if (s->sink) s->sink->setScaler(quality);
	return LM_OK;
}

lm_error lm_server_set_low_latency(lm_server* s, int enable)
{
	if (!s) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.lowLatency = enable != 0;
	return LM_OK;
}

lm_error lm_server_set_hw_decode(lm_server* s, int enable)
{
	if (!s) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.hardwareDecode = enable != 0;
	return LM_OK;
}

lm_error lm_server_set_display(lm_server* s, uint32_t width, uint32_t height, uint32_t max_fps)
{
	if (!s || !width || !height || !max_fps) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.displayWidth = width;
	s->options.displayHeight = height;
	s->options.displayFps = max_fps;
	return LM_OK;
}

lm_error lm_server_set_second_device(lm_server* s, int replace)
{
	if (!s) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.replaceSecond = replace != 0;
	return LM_OK;
}

lm_error lm_server_set_pin(lm_server* s, lm_pin_mode mode, const char* pin)
{
	if (!s || mode < LM_PIN_OFF || mode > LM_PIN_FIXED) return LM_E_INVALID_ARG;
	if (mode == LM_PIN_FIXED && (!pin || !pin[0] || strlen(pin) >= LM_PIN_MAX)) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.pinMode = mode;
	s->options.pin = mode == LM_PIN_FIXED ? pin : "";
	return LM_OK;
}

static std::wstring toWide(const char* utf8)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	std::wstring out(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], n);
	return out;
}

static void toUtf8(const std::wstring& wide, char* out, int size)
{
	// a name too long for the buffer comes out empty rather than cut mid-character
	if (WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out, size, NULL, NULL) == 0 && size > 0) out[0] = '\0';
}

int lm_audio_devices(lm_audio_device* out, int max)
{
	std::vector<LmAudioSink::Device> list = LmAudioSink::devices();
	for (int i = 0; out && i < max && i < (int)list.size(); i++) {
		toUtf8(list[i].id, out[i].id, sizeof(out[i].id));
		toUtf8(list[i].name, out[i].name, sizeof(out[i].name));
	}
	return (int)list.size();
}

lm_error lm_server_set_follow_volume(lm_server* s, int enable)
{
	if (!s) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.followVolume = enable != 0;
	return LM_OK;
}

lm_error lm_server_set_audio_device(lm_server* s, const char* id)
{
	if (!s) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	s->options.audioDevice = id ? toWide(id) : L"";
	if (s->audio) s->audio->setDevice(s->options.audioDevice);
	return LM_OK;
}

lm_error lm_server_set_audio(lm_server* s, int enable)
{
	if (!s) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	if (!enable) {
		delete s->audio;
		s->audio = NULL;
	} else if (!s->audio) {
		s->audio = new (std::nothrow) LmAudioSink();
		if (!s->audio) return LM_E_NO_MEMORY;
		s->audio->setDevice(s->options.audioDevice);
	}
	return LM_OK;
}

lm_error lm_server_set_window(lm_server* s, void* hwnd)
{
	if (!s || (hwnd && !IsWindow((HWND)hwnd))) return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	delete s->sink;
	s->sink = NULL;
	if (!hwnd) return LM_OK;
	s->sink = new (std::nothrow) LmVideoSink();
	if (!s->sink) return LM_E_NO_MEMORY;
	if (!s->sink->open((HWND)hwnd)) {
		delete s->sink;
		s->sink = NULL;
		return LM_E_INTERNAL;
	}
	s->sink->setColour(s->options.colour);
	s->sink->setScaler(s->options.scaler);
	return LM_OK;
}

lm_error lm_server_create(const lm_callbacks* cb, lm_server** out)
{
	if (!cb || !out) return LM_E_INVALID_ARG;
	*out = new (std::nothrow) lm_server(*cb);
	return *out ? LM_OK : LM_E_NO_MEMORY;
}

lm_error lm_server_start(lm_server* s, const lm_config* cfg)
{
	if (!s || !cfg || !cfg->name || !cfg->name[0] || strlen(cfg->name) >= LM_NAME_MAX)
		return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	// Check up front so the shell can tell the user before the phone connects
	if (!MfH264Decoder::available()) return LM_E_NO_H264_DECODER;

	FgAirplayServer* impl = new (std::nothrow) FgAirplayServer();
	if (!impl) return LM_E_NO_MEMORY;
	lm_error err = impl->start(cfg->name, cfg->raop_port, cfg->airplay_port, &s->cb, s->sink, s->audio, s->options);
	if (err != LM_OK) {
		delete impl;
		return err;
	}
	s->impl = impl;
	return LM_OK;
}

void lm_server_stop(lm_server* s)
{
	if (!s || !s->impl) return;
	s->impl->stop();
	delete s->impl;
	s->impl = NULL;
}

void lm_server_destroy(lm_server* s)
{
	lm_server_stop(s);
	delete s;
}

const char* lm_error_string(lm_error e)
{
	switch (e) {
	case LM_OK:                 return "ok";
	case LM_E_INVALID_ARG:      return "invalid argument";
	case LM_E_RUNNING:          return "server already running";
	case LM_E_NO_MEMORY:        return "out of memory";
	case LM_E_RAOP_PORT:        return "RTSP port already in use";
	case LM_E_AIRPLAY_PORT:     return "AirPlay HTTP port already in use";
	case LM_E_MDNS_UNAVAILABLE: return "mDNS registration needs Windows 10 1809 or later";
	case LM_E_MDNS_REGISTER:    return "Windows refused the mDNS service registration";
	case LM_E_INTERNAL:         return "internal error";
	case LM_E_NO_H264_DECODER:  return "No H.264 decoder found. On Windows N editions install the Media Feature Pack.";
	case LM_E_ADDRESS:          return "this machine has no such address";
	}
	return "unknown error";
}
