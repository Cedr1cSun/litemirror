// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#pragma once
#include "litemirror.h"

class CSDLPlayer;

// Runs the receiver through the C API and feeds the first device that
// connects to the player; frames from any other device are ignored.
class CAirServer
{
public:
	CAirServer();
	~CAirServer();

	lm_error start(CSDLPlayer* pPlayer);
	void stop();
	bool running() const { return m_running; }

private:
	static void onConnected(void* user, const char* id, const char* name);
	static void onDisconnected(void* user, const char* id, const char* name);
	static void onVideo(void* user, const char* id, const lm_video_frame* frame);
	static void onAudio(void* user, const char* id, const lm_audio_frame* frame);
	static void onLog(void* user, lm_log_level level, const char* msg);
	bool mine(const char* id);

	lm_server*	m_server;
	CSDLPlayer*	m_player;
	bool		m_running;
	char		m_deviceId[128];
};
