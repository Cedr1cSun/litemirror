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
	LM_E_NO_H264_DECODER,    /* no Media Foundation H.264 decoder: Windows N without the Media Feature Pack */
	LM_E_ADDRESS             /* lm_server_set_address: not an address of this machine */
} lm_error;

typedef enum lm_log_level {  /* syslog values, same as the core */
	LM_LOG_ERROR   = 3,
	LM_LOG_WARNING = 4,
	LM_LOG_INFO    = 6,
	LM_LOG_DEBUG   = 7
} lm_log_level;

#define LM_NAME_MAX 128      /* receiver name buffer size, including the NUL */
#define LM_PIN_MAX  64       /* fixed PIN buffer size, including the NUL */

typedef enum lm_pin_mode {
	LM_PIN_OFF = 0,
	LM_PIN_RANDOM,           /* four digits chosen per connection, given through on_pin */
	LM_PIN_FIXED             /* the pin passed to lm_server_set_pin */
} lm_pin_mode;

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
	/* LM_PIN_RANDOM: show this code on screen; NULL once the phone got it right or gave up */
	void (*on_pin)         (void* user, const char* pin);
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
/* Make the phone ask for a PIN before it may connect (HTTP Digest, as an
 * Apple TV with a password). Set it before lm_server_start(); pin is 1 to
 * LM_PIN_MAX-1 bytes for LM_PIN_FIXED and ignored otherwise. */
LM_API lm_error    lm_server_set_pin   (lm_server* s, lm_pin_mode mode, const char* pin);
/* A second phone while one is connected: 0 (default) turns it away with
 * the busy answer an Apple TV gives, 1 lets it in and drops the first.
 * Set it before lm_server_start(). */
LM_API lm_error    lm_server_set_second_device(lm_server* s, int replace);
/* Listen and announce on one IPv4 address of this machine only (dotted,
 * e.g. the Wi-Fi adapter's), for machines with VPN or virtual adapters.
 * NULL or "" means every address. Set it before lm_server_start(), which
 * fails with LM_E_ADDRESS when the machine has no such address. */
LM_API lm_error    lm_server_set_address(lm_server* s, const char* ipv4);
/* The display the phone is told about, so what it encodes at: a smaller
 * size or frame rate costs less bandwidth and CPU. Default 1920x1080 at
 * 60. Set it before lm_server_start(); all three must be non-zero. */
LM_API lm_error    lm_server_set_display(lm_server* s, uint32_t width, uint32_t height, uint32_t max_fps);
/* Decode on the GPU (default, 1) or on the CPU (0), for machines whose GPU
 * decoder misbehaves. Set it before lm_server_start(). */
LM_API lm_error    lm_server_set_hw_decode(lm_server* s, int enable);
/* Picture adjustments for the window set with lm_server_set_window. May
 * be changed at any time, also while mirroring. */
typedef struct lm_colour {
	int   matrix;        /* 0 auto (BT.709 for HD, BT.601 below), 1 BT.601, 2 BT.709 */
	int   range;         /* 0 auto (studio), 1 studio 16-235, 2 full 0-255 */
	float brightness;    /* -1..1, 0 = as decoded; the GPU's own filter range */
	float contrast;
	float saturation;
	float hue;
} lm_colour;
LM_API lm_error    lm_server_set_colour(lm_server* s, const lm_colour* colour);
/* On (default): the decoder gives out each picture as soon as it can and
 * the window shows it without waiting for the display's refresh. Off:
 * smoother, a frame or two later. Set it before lm_server_start(). */
LM_API lm_error    lm_server_set_low_latency(lm_server* s, int enable);
LM_API lm_error    lm_server_start  (lm_server* s, const lm_config* cfg);
LM_API void        lm_server_stop   (lm_server* s);     /* blocks until workers exit */
LM_API void        lm_server_destroy(lm_server* s);     /* stops first if needed */
LM_API const char* lm_error_string  (lm_error e);       /* English, for logs only */

#ifdef __cplusplus
}
#endif
