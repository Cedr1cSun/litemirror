// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// C API on top of FgAirplayServer. The bridge turns the C++ callback
// interface into the function pointers of lm_callbacks.

#include <new>
#include <string.h>
#include "litemirror.h"
#include "FgAirplayServer.h"

namespace {

class Bridge : public IAirServerCallback {
public:
	explicit Bridge(const lm_callbacks& cb) : m_cb(cb) {}

	void connected(const char* name, const char* id) {
		if (m_cb.on_connected) m_cb.on_connected(m_cb.user, id, name);
	}
	void disconnected(const char* name, const char* id) {
		if (m_cb.on_disconnected) m_cb.on_disconnected(m_cb.user, id, name);
	}
	void outputAudio(SFgAudioFrame* d, const char*, const char* id) {
		if (!m_cb.on_audio) return;
		lm_audio_frame f = { d->pts, d->sampleRate, d->channels, d->bitsPerSample, d->data, d->dataLen };
		m_cb.on_audio(m_cb.user, id, &f);
	}
	void outputVideo(SFgVideoFrame* d, const char*, const char* id) {
		if (!m_cb.on_video) return;
		lm_video_frame f;
		f.pts = d->pts;
		f.is_key = d->isKey;
		f.width = d->width;
		f.height = d->height;
		f.plane[0] = d->data;                   // NV12: Y, then UV at dataLen[0]
		f.plane[1] = d->data + d->dataLen[0];
		f.pitch[0] = d->pitch[0];
		f.pitch[1] = d->pitch[1];
		m_cb.on_video(m_cb.user, id, &f);
	}
	void audioSetVolume(float volume, const char*, const char* id) {
		if (m_cb.on_volume) m_cb.on_volume(m_cb.user, id, volume);
	}
	void audioSetMetadata(const void* buffer, int buflen, const char*, const char* id) {
		if (m_cb.on_metadata) m_cb.on_metadata(m_cb.user, id, buffer, (size_t)buflen);
	}
	void audioSetCoverArt(const void* buffer, int buflen, const char*, const char* id) {
		if (m_cb.on_cover_art) m_cb.on_cover_art(m_cb.user, id, buffer, (size_t)buflen);
	}
	void videoPlay(char* url, double volume, double startPos) {
		if (m_cb.on_play_url) m_cb.on_play_url(m_cb.user, url, (float)volume, startPos);
	}
	void videoGetPlayInfo(double* duration, double* position, double* rate) {
		*duration = *position = *rate = 0;
	}
	void log(int level, const char* msg) {
		if (m_cb.on_log) m_cb.on_log(m_cb.user, (lm_log_level)level, msg);
	}

private:
	lm_callbacks m_cb;
};

} // namespace

struct lm_server {
	explicit lm_server(const lm_callbacks& cb) : bridge(cb), impl(NULL) {}
	Bridge bridge;
	FgAirplayServer* impl;   // non-NULL while running
};

lm_error lm_server_create(const lm_callbacks* cb, lm_server** out)
{
	if (!cb || !out) return LM_E_INVALID_ARG;
	*out = new (std::nothrow) lm_server(*cb);
	return *out ? LM_OK : LM_E_NO_MEMORY;
}

lm_error lm_server_start(lm_server* s, const lm_config* cfg)
{
	if (!s || !cfg || !cfg->name || !cfg->name[0] || strlen(cfg->name) >= AIRPLAY_NAME_LEN)
		return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;

	FgAirplayServer* impl = new (std::nothrow) FgAirplayServer();
	if (!impl) return LM_E_NO_MEMORY;
	lm_error err = impl->start(cfg->name, cfg->raop_port, cfg->airplay_port, &s->bridge);
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
	}
	return "unknown error";
}
