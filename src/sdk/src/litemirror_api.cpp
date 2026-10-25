// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// The C API: argument checks and lifetime; FgAirplayServer does the work.

#include <new>
#include <string.h>
#include "litemirror.h"
#include "FgAirplayServer.h"
#include "MfH264Decoder.h"

struct lm_server {
	explicit lm_server(const lm_callbacks& callbacks) : cb(callbacks), impl(NULL) {}
	lm_callbacks cb;
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
	if (!s || !cfg || !cfg->name || !cfg->name[0] || strlen(cfg->name) >= LM_NAME_MAX)
		return LM_E_INVALID_ARG;
	if (s->impl) return LM_E_RUNNING;
	// Check up front so the shell can tell the user before the phone connects
	if (!MfH264Decoder::available()) return LM_E_NO_H264_DECODER;

	FgAirplayServer* impl = new (std::nothrow) FgAirplayServer();
	if (!impl) return LM_E_NO_MEMORY;
	lm_error err = impl->start(cfg->name, cfg->raop_port, cfg->airplay_port, &s->cb);
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
	}
	return "unknown error";
}
