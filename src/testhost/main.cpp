// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Headless host for litemirror.dll. Reads commands from stdin and reports
// server events on stdout, one line each, so scripts can drive the server
// without a window:
//
//   start <name> <raopPort> <airplayPort>   -> OK | FAIL <code> <message>
//   stop                                    -> OK
//   quit
//
// Events: EVENT connected <name> <id>, EVENT disconnected <name> <id>,
// EVENT video <w>x<h>, EVENT audio <bytes>, EVENT play <url>, LOG <level> <msg>

#include <stdio.h>
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
static void on_play_url(void*, const char* url, float, double)
{
	printf("EVENT play %s\n", url);
}
static void on_log(void*, lm_log_level level, const char* msg)
{
	printf("LOG %d %s\n", (int)level, msg);
}

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);

	lm_callbacks cb;
	memset(&cb, 0, sizeof(cb));
	cb.on_connected = on_connected;
	cb.on_disconnected = on_disconnected;
	cb.on_audio = on_audio;
	cb.on_video = on_video;
	cb.on_play_url = on_play_url;
	cb.on_log = on_log;

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
		unsigned raopPort = 0, airplayPort = 0;

		if (sscanf(line, "start %127s %u %u", name, &raopPort, &airplayPort) == 3) {
			lm_server_stop(server);
			lm_config cfg = { name, (uint16_t)raopPort, (uint16_t)airplayPort };
			err = lm_server_start(server, &cfg);
			if (err == LM_OK) printf("OK\n");
			else printf("FAIL %d %s\n", (int)err, lm_error_string(err));
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
