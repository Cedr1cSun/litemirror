// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#pragma once
#include <windows.h>
#include <queue>
#include <vector>
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "litemirror.h"
#include "CAirServer.h"

typedef struct SDemoAudioFrame {
	unsigned int dataTotal;
	unsigned int dataLeft;
	unsigned char* data;
} SDemoAudioFrame;

// SDL2 window: NV12 frames go to a GPU texture, PCM to the audio device.
// Callbacks arrive on the receiver's threads; rendering happens on the
// main thread inside loopEvents(), so frames are copied once and handed
// over through an SDL event.
class CSDLPlayer
{
public:
	CSDLPlayer();
	~CSDLPlayer();

	bool init();
	void loopEvents();

	void outputVideo(const lm_video_frame* frame);
	void outputAudio(const lm_audio_frame* frame);

private:
	void startServer();
	void stopServer();
	void render();
	void initAudio(const lm_audio_frame* frame);
	void unInitAudio();
	static void sdlAudioCallback(void* userdata, Uint8* stream, int len);

	SDL_Window*		m_window;
	SDL_Renderer*	m_renderer;
	SDL_Texture*	m_texture;
	int				m_texW, m_texH;

	// latest decoded frame, copied out of the callback
	HANDLE					m_mutexVideo;
	std::vector<uint8_t>	m_y, m_uv;
	int						m_frameW, m_frameH, m_pitchY, m_pitchUV;
	LONG					m_framePending;

	SDL_AudioDeviceID				m_audio;
	unsigned						m_audioRate, m_audioChannels;
	HANDLE							m_mutexAudio;
	std::queue<SDemoAudioFrame*>	m_queueAudio;

	CAirServer m_server;
};
