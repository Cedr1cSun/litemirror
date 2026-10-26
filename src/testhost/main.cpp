// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Headless host for litemirror.dll. Reads commands from stdin and reports
// server events on stdout, one line each, so scripts can drive the server
// without a window:
//
//   start <name> <raopPort> <airplayPort>   -> OK | FAIL <code> <message>
//   stop                                    -> OK
//   pin off | pin random | pin fixed <code> -> OK | FAIL <code> <message>   (before start)
//   second reject | second replace          -> OK | FAIL <code> <message>   (before start)
//   quit
//
// Events: EVENT connected <name> <id>, EVENT disconnected <name> <id>,
// EVENT video <w>x<h>, EVENT audio <bytes>, EVENT volume <id> <db>,
// EVENT metadata <id> <bytes>, EVENT coverart <id> <bytes>, EVENT play <url>,
// EVENT pin <code|->, LOG <level> <msg>

#include <crtdbg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "litemirror.h"

static unsigned g_video, g_audio;

static void on_connected(void*, const char* id, const char* name)
{
	printf("EVENT connected %s %s\n", name, id);
}
static void on_disconnected(void*, const char* id, const char* name)
{
	printf("EVENT disconnected %s %s\n", name, id);
}
static void on_audio(void*, const char*, const lm_audio_frame* f)
{
	// shortcut: report every 100th frame, enough to see audio flowing
	if (g_audio++ % 100 == 0) printf("EVENT audio %zu\n", f->size);
}
static void on_video(void*, const char*, const lm_video_frame* f)
{
	if (g_video++ % 60 == 0) printf("EVENT video %ux%u\n", f->width, f->height);
}
static void on_volume(void*, const char* id, float db)
{
	printf("EVENT volume %s %.1f\n", id, db);
}
static void on_metadata(void*, const char* id, const void*, size_t size)
{
	printf("EVENT metadata %s %zu\n", id, size);
}
static void on_cover_art(void*, const char* id, const void*, size_t size)
{
	printf("EVENT coverart %s %zu\n", id, size);
}
static void on_play_url(void*, const char* url, float, double)
{
	printf("EVENT play %s\n", url);
}
static void on_log(void*, lm_log_level level, const char* msg)
{
	printf("LOG %d %s\n", (int)level, msg);
}
static void on_pin(void*, const char* pin)
{
	printf("EVENT pin %s\n", pin ? pin : "-");
}

static void report(lm_error err)
{
	if (err == LM_OK) printf("OK\n");
	else printf("FAIL %d %s\n", (int)err, lm_error_string(err));
}

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	// Headless: a failed assert or abort() must print and exit, not pop a dialog
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);

	lm_callbacks cb;
	memset(&cb, 0, sizeof(cb));
	cb.on_connected = on_connected;
	cb.on_disconnected = on_disconnected;
	cb.on_audio = on_audio;
	cb.on_video = on_video;
	cb.on_volume = on_volume;
	cb.on_metadata = on_metadata;
	cb.on_cover_art = on_cover_art;
	cb.on_play_url = on_play_url;
	cb.on_log = on_log;
	cb.on_pin = on_pin;

	lm_server* server = NULL;
	lm_error err = lm_server_create(&cb, &server);
	if (err != LM_OK) {
		printf("FAIL %d %s\n", (int)err, lm_error_string(err));
		return 1;
	}

	char line[512];
	printf("READY\n");
	while (fgets(line, sizeof(line), stdin)) {
		char name[128] = {0};
		char code[64] = {0};
		unsigned raopPort = 0, airplayPort = 0;

		if (sscanf(line, "start %127s %u %u", name, &raopPort, &airplayPort) == 3) {
			lm_server_stop(server);
			lm_config cfg = { name, (uint16_t)raopPort, (uint16_t)airplayPort };
			report(lm_server_start(server, &cfg));
		} else if (strncmp(line, "pin off", 7) == 0) {
			report(lm_server_set_pin(server, LM_PIN_OFF, NULL));
		} else if (strncmp(line, "pin random", 10) == 0) {
			report(lm_server_set_pin(server, LM_PIN_RANDOM, NULL));
		} else if (sscanf(line, "pin fixed %63s", code) == 1) {
			report(lm_server_set_pin(server, LM_PIN_FIXED, code));
		} else if (strncmp(line, "second reject", 13) == 0) {
			report(lm_server_set_second_device(server, 0));
		} else if (strncmp(line, "second replace", 14) == 0) {
			report(lm_server_set_second_device(server, 1));
		} else if (strncmp(line, "stop", 4) == 0) {
			lm_server_stop(server);
			printf("OK\n");
		} else if (strncmp(line, "quit", 4) == 0) {
			break;
		} else {
			printf("ERR unknown command\n");
		}
	}
	lm_server_destroy(server);
	return 0;
}
