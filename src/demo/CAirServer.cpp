// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "CAirServer.h"
#include "CSDLPlayer.h"

#pragma comment(lib, "ws2_32.lib")

CAirServer::CAirServer()
	: m_server(NULL), m_player(NULL), m_running(false)
{
	m_deviceId[0] = '\0';
	lm_callbacks cb;
	memset(&cb, 0, sizeof(cb));
	cb.user = this;
	cb.on_connected = onConnected;
	cb.on_disconnected = onDisconnected;
	cb.on_video = onVideo;
	cb.on_audio = onAudio;
	cb.on_log = onLog;
	lm_server_create(&cb, &m_server);
}

CAirServer::~CAirServer()
{
	lm_server_destroy(m_server);
}

lm_error CAirServer::start(CSDLPlayer* pPlayer)
{
	stop();
	m_player = pPlayer;

	char host[256] = {0};
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	if (gethostname(host, sizeof(host) - 1) != 0 || host[0] == '\0') {
		DWORD n = sizeof(host) - 1;
		GetComputerNameA(host, &n);
	}
	char name[128];
	snprintf(name, sizeof(name), "LiteMirror (%s)", host);

	lm_config cfg = { name, 5001, 7001 };
	lm_error err = lm_server_start(m_server, &cfg);
	m_running = err == LM_OK;
	return err;
}

void CAirServer::stop()
{
	lm_server_stop(m_server);
	m_running = false;
	m_deviceId[0] = '\0';
}

bool CAirServer::mine(const char* id)
{
	if (m_deviceId[0] == '\0' && id != NULL) {
		strncpy(m_deviceId, id, sizeof(m_deviceId) - 1);
	}
	return id != NULL && strcmp(m_deviceId, id) == 0;
}

void CAirServer::onConnected(void* user, const char* id, const char* name)
{
	CAirServer* self = (CAirServer*)user;
	self->mine(id);
	printf("Connected: %s (%s)\n", name, id);
}

void CAirServer::onDisconnected(void* user, const char* id, const char* name)
{
	CAirServer* self = (CAirServer*)user;
	printf("Disconnected: %s (%s)\n", name, id);
	if (self->mine(id)) {
		self->m_deviceId[0] = '\0';
	}
}

void CAirServer::onVideo(void* user, const char* id, const lm_video_frame* frame)
{
	CAirServer* self = (CAirServer*)user;
	if (self->m_player && self->mine(id)) {
		self->m_player->outputVideo(frame);
	}
}

void CAirServer::onAudio(void* user, const char* id, const lm_audio_frame* frame)
{
	CAirServer* self = (CAirServer*)user;
	if (self->m_player && self->mine(id)) {
		self->m_player->outputAudio(frame);
	}
}

void CAirServer::onLog(void*, lm_log_level level, const char* msg)
{
	if (level <= LM_LOG_INFO) {
		printf("%s\n", msg);
	}
}
