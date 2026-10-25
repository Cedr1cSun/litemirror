/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LiteMirror contributors
 *
 * Public C API of litemirror.dll. Everything a shell (console, WPF, ...)
 * needs to receive AirPlay mirroring and audio from an iOS device.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef AIRPLAY2_EXPORTS
#define LM_API __declspec(dllexport)
#else
#define LM_API __declspec(dllimport)
#pragma comment(lib, "litemirror.lib")
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lm_server lm_server;          /* opaque */

typedef enum lm_error {
	LM_OK = 0,
	LM_E_INVALID_ARG,        /* NULL pointer, empty name, name too long */
	LM_E_RUNNING,            /* start() while already started */
	LM_E_NO_MEMORY,
	LM_E_RAOP_PORT,          /* RTSP port bind failed */
	LM_E_AIRPLAY_PORT,       /* HTTP port bind failed */
	LM_E_MDNS_UNAVAILABLE,   /* dnsapi.dll lacks DnsServiceRegister (Windows 10 before 1809) */
	LM_E_MDNS_REGISTER,      /* Windows refused or timed out registering the service */
	LM_E_INTERNAL,
	LM_E_NO_H264_DECODER     /* no Media Foundation H.264 decoder: Windows N without the Media Feature Pack */
} lm_error;

typedef enum lm_log_level {  /* syslog values, same as the core */
	LM_LOG_ERROR   = 3,
	LM_LOG_WARNING = 4,
	LM_LOG_INFO    = 6,
	LM_LOG_DEBUG   = 7
} lm_log_level;

#define LM_NAME_MAX 128      /* receiver name buffer size, including the NUL */

typedef struct lm_config {
	const char* name;        /* receiver name shown on the iPhone, UTF-8, under LM_NAME_MAX bytes */
	uint16_t    raop_port;   /* 0 = let the OS pick */
	uint16_t    airplay_port;
} lm_config;

/* NV12: plane[0] is Y, plane[1] is interleaved UV at half height. The
 * planes are valid only until the callback returns. */
typedef struct lm_video_frame {
	uint64_t       pts;
	int            is_key;
	uint32_t       width, height;
	const uint8_t* plane[2];
	uint32_t       pitch[2];
} lm_video_frame;

/* Interleaved PCM, signed 16-bit. Valid only until the callback returns. */
typedef struct lm_audio_frame {
	uint64_t       pts;
	uint32_t       sample_rate;
	uint16_t       channels;
	uint16_t       bits_per_sample;
	const uint8_t* data;
	size_t         size;
} lm_audio_frame;

/* Every callback runs on an internal thread; return quickly, copy what you
 * keep. Any member may be NULL. After lm_server_stop() returns no callback
 * fires again. device_id identifies one iPhone across audio and mirror. */
typedef struct lm_callbacks {
	void* user;
	void (*on_connected)   (void* user, const char* device_id, const char* name);
	void (*on_disconnected)(void* user, const char* device_id, const char* name);
	void (*on_video)       (void* user, const char* device_id, const lm_video_frame* frame);
	void (*on_audio)       (void* user, const char* device_id, const lm_audio_frame* frame);
	void (*on_volume)      (void* user, const char* device_id, float db);  /* -30..0, -144 = mute */
	void (*on_metadata)    (void* user, const char* device_id, const void* dmap, size_t size);
	void (*on_cover_art)   (void* user, const char* device_id, const void* image, size_t size);
	void (*on_play_url)    (void* user, const char* url, float volume, double start_pos);
	void (*on_log)         (void* user, lm_log_level level, const char* msg);
	/* the picture size changed (first frame, phone rotated); also with a window set */
	void (*on_video_size)  (void* user, const char* device_id, uint32_t width, uint32_t height);
} lm_callbacks;

LM_API lm_error    lm_server_create (const lm_callbacks* cb, lm_server** out);
/* Draw the mirror picture into this window (an HWND), letterboxed, straight
 * from the GPU decoder; on_video is then not called. Set it before
 * lm_server_start(); NULL goes back to on_video. LM_E_RUNNING while
 * started, LM_E_INTERNAL when Direct3D 11 is unavailable. */
LM_API lm_error    lm_server_set_window(lm_server* s, void* hwnd);
/* Play the audio on the default output device; on_audio is then not
 * called. Set it before lm_server_start(); LM_E_RUNNING while started. */
LM_API lm_error    lm_server_set_audio (lm_server* s, int enable);
LM_API lm_error    lm_server_start  (lm_server* s, const lm_config* cfg);
LM_API void        lm_server_stop   (lm_server* s);     /* blocks until workers exit */
LM_API void        lm_server_destroy(lm_server* s);     /* stops first if needed */
LM_API const char* lm_error_string  (lm_error e);       /* English, for logs only */

#ifdef __cplusplus
}
#endif
