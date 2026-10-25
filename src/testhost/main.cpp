// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Headless host for litemirror.dll. Reads commands from stdin and reports
// server events on stdout, one line each, so scripts can drive the server
// without a window:
//
//   start <name> <raopPort> <airplayPort>   -> OK | FAIL
//   stop                                    -> OK
//   quit
//
// Events: EVENT connected <name> <id>, EVENT disconnected <name> <id>,
// EVENT video <w>x<h>, EVENT audio <bytes>, EVENT play <url>, LOG <level> <msg>

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Airplay2Head.h"

class HostCallback : public IAirServerCallback {
public:
	HostCallback() : m_video(0), m_audio(0) {}

	void connected(const char* remoteName, const char* remoteDeviceId) {
		printf("EVENT connected %s %s\n", remoteName, remoteDeviceId);
	}
	void disconnected(const char* remoteName, const char* remoteDeviceId) {
		printf("EVENT disconnected %s %s\n", remoteName, remoteDeviceId);
	}
	void outputAudio(SFgAudioFrame* data, const char*, const char*) {
		// shortcut: report every 100th frame, enough to see audio flowing
		if (m_audio++ % 100 == 0) printf("EVENT audio %u\n", data->dataLen);
	}
	void outputVideo(SFgVideoFrame* data, const char*, const char*) {
		if (m_video++ % 60 == 0) printf("EVENT video %ux%u\n", data->width, data->height);
	}
	void videoPlay(char* url, double, double) {
		printf("EVENT play %s\n", url);
	}
	void videoGetPlayInfo(double* duration, double* position, double* rate) {
		*duration = 0; *position = 0; *rate = 0;
	}
	void log(int level, const char* msg) {
		printf("LOG %d %s\n", level, msg);
	}

private:
	unsigned m_video;
	unsigned m_audio;
};

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	HostCallback callback;
	void* server = NULL;
	char line[512];

	printf("READY\n");
	while (fgets(line, sizeof(line), stdin)) {
		char name[AIRPLAY_NAME_LEN] = {0};
		unsigned raopPort = 0, airplayPort = 0;

		if (sscanf(line, "start %127s %u %u", name, &raopPort, &airplayPort) == 3) {
			if (server) fgServerStop(server);
			server = fgServerStart(name, raopPort, airplayPort, &callback);
			printf(server ? "OK\n" : "FAIL\n");
		} else if (strncmp(line, "stop", 4) == 0) {
			if (server) fgServerStop(server);
			server = NULL;
			printf("OK\n");
		} else if (strncmp(line, "quit", 4) == 0) {
			break;
		} else {
			printf("ERR unknown command\n");
		}
	}
	if (server) fgServerStop(server);
	return 0;
}
