// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include "CSDLPlayer.h"
#include <stdio.h>
#include "CAutoLock.h"

#define FRAME_EVENT 1
static const char* TITLE_STOPPED = "LiteMirror - stopped   [s] start  [q] stop";
static const char* TITLE_RUNNING = "LiteMirror - running   [s] start  [q] stop";

CSDLPlayer::CSDLPlayer()
	: m_window(NULL), m_renderer(NULL), m_texture(NULL), m_texW(0), m_texH(0)
	, m_frameW(0), m_frameH(0), m_pitchY(0), m_pitchUV(0), m_framePending(0)
	, m_reportedW(0), m_reportedH(0), m_statFrames(0), m_statStart(0)
	, m_audio(0), m_audioRate(0), m_audioChannels(0)
{
	m_mutexVideo = CreateMutex(NULL, FALSE, NULL);
	m_mutexAudio = CreateMutex(NULL, FALSE, NULL);
}

CSDLPlayer::~CSDLPlayer()
{
	m_server.stop();
	unInitAudio();
	if (m_texture) SDL_DestroyTexture(m_texture);
	if (m_renderer) SDL_DestroyRenderer(m_renderer);
	if (m_window) SDL_DestroyWindow(m_window);
	SDL_Quit();
	CloseHandle(m_mutexVideo);
	CloseHandle(m_mutexAudio);
}

bool CSDLPlayer::init()
{
	SDL_SetMainReady();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		printf("Could not initialize SDL: %s\n", SDL_GetError());
		return false;
	}
	m_window = SDL_CreateWindow(TITLE_STOPPED, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                            960, 540, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!m_window) {
		printf("Could not create window: %s\n", SDL_GetError());
		return false;
	}
	m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!m_renderer) {
		printf("Could not create renderer: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
	SDL_RenderClear(m_renderer);
	SDL_RenderPresent(m_renderer);

	startServer();
	return true;
}

void CSDLPlayer::startServer()
{
	lm_error err = m_server.start(this);
	if (err != LM_OK) {
		SDL_SetWindowTitle(m_window, TITLE_STOPPED);
		MessageBoxA(NULL, lm_error_string(err), "LiteMirror", MB_OK | MB_ICONERROR);
		return;
	}
	SDL_SetWindowTitle(m_window, TITLE_RUNNING);
}

void CSDLPlayer::stopServer()
{
	m_server.stop();
	unInitAudio();
	SDL_SetWindowTitle(m_window, TITLE_STOPPED);
}

void CSDLPlayer::loopEvents()
{
	SDL_Event event;
	while (SDL_WaitEvent(&event)) {
		switch (event.type) {
		case SDL_USEREVENT:
			if (event.user.code == FRAME_EVENT) {
				InterlockedExchange(&m_framePending, 0);
				render();
			}
			break;
		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
			    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				render();
			}
			break;
		case SDL_KEYUP:
			if (event.key.keysym.sym == SDLK_q) stopServer();
			else if (event.key.keysym.sym == SDLK_s) startServer();
			break;
		case SDL_QUIT:
			printf("Quit requested, quitting.\n");
			stopServer();
			return;
		}
	}
}

void CSDLPlayer::outputVideo(const lm_video_frame* f)
{
	if (f->width == 0 || f->height == 0) {
		return;
	}
	if ((int)f->width != m_reportedW || (int)f->height != m_reportedH) {
		m_reportedW = f->width;
		m_reportedH = f->height;
		printf("Video: %ux%u\n", f->width, f->height);
	}
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	if (m_statStart == 0) m_statStart = now.QuadPart;
	m_statFrames++;
	if (now.QuadPart - m_statStart >= 5 * freq.QuadPart) {
		printf("Video: %.1f fps\n", m_statFrames * (double)freq.QuadPart / (now.QuadPart - m_statStart));
		m_statFrames = 0;
		m_statStart = now.QuadPart;
	}
	{
		CAutoLock oLock(m_mutexVideo, "outputVideo");
		size_t ySize = (size_t)f->pitch[0] * f->height;
		size_t uvSize = (size_t)f->pitch[1] * ((f->height + 1) / 2);
		m_y.assign(f->plane[0], f->plane[0] + ySize);
		m_uv.assign(f->plane[1], f->plane[1] + uvSize);
		m_frameW = f->width;
		m_frameH = f->height;
		m_pitchY = f->pitch[0];
		m_pitchUV = f->pitch[1];
	}
	// one event per rendered frame; if the main thread is behind, it will pick up the latest
	if (InterlockedCompareExchange(&m_framePending, 1, 0) == 0) {
		SDL_Event ev;
		memset(&ev, 0, sizeof(ev));
		ev.type = SDL_USEREVENT;
		ev.user.code = FRAME_EVENT;
		SDL_PushEvent(&ev);
	}
}

void CSDLPlayer::render()
{
	{
		CAutoLock oLock(m_mutexVideo, "render");
		if (m_frameW == 0) {
			return;
		}
		if (!m_texture || m_texW != m_frameW || m_texH != m_frameH) {
			if (m_texture) SDL_DestroyTexture(m_texture);
			m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STREAMING, m_frameW, m_frameH);
			m_texW = m_frameW;
			m_texH = m_frameH;
		}
		SDL_UpdateNVTexture(m_texture, NULL, m_y.data(), m_pitchY, m_uv.data(), m_pitchUV);
	}

	// letterbox into the window, keeping the phone's aspect ratio
	int winW = 0, winH = 0;
	SDL_GetRendererOutputSize(m_renderer, &winW, &winH);
	SDL_Rect dst;
	if ((long long)winW * m_texH > (long long)winH * m_texW) {
		dst.h = winH;
		dst.w = (int)((long long)winH * m_texW / m_texH);
	} else {
		dst.w = winW;
		dst.h = (int)((long long)winW * m_texH / m_texW);
	}
	dst.x = (winW - dst.w) / 2;
	dst.y = (winH - dst.h) / 2;

	SDL_RenderClear(m_renderer);
	SDL_RenderCopy(m_renderer, m_texture, NULL, &dst);
	SDL_RenderPresent(m_renderer);
}

void CSDLPlayer::outputAudio(const lm_audio_frame* f)
{
	if (f->channels == 0 || f->size == 0) {
		return;
	}
	initAudio(f);

	SDemoAudioFrame* clone = new SDemoAudioFrame();
	clone->dataTotal = (unsigned int)f->size;
	clone->dataLeft = clone->dataTotal;
	clone->data = new uint8_t[clone->dataTotal];
	memcpy(clone->data, f->data, clone->dataTotal);

	size_t queued;
	{
		CAutoLock oLock(m_mutexAudio, "outputAudio");
		m_queueAudio.push(clone);
		queued = m_queueAudio.size();
	}
	if (m_audio && queued > 5) {
		SDL_PauseAudioDevice(m_audio, 0);   // a little buffered before playback starts
	}
}

void CSDLPlayer::initAudio(const lm_audio_frame* f)
{
	if (m_audio && (f->sample_rate != m_audioRate || f->channels != m_audioChannels)) {
		unInitAudio();
	}
	if (m_audio) {
		return;
	}
	SDL_AudioSpec want, have;
	memset(&want, 0, sizeof(want));
	want.freq = f->sample_rate;
	want.format = AUDIO_S16SYS;
	want.channels = (Uint8)f->channels;
	want.samples = 1920;
	want.callback = sdlAudioCallback;
	want.userdata = this;
	m_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!m_audio) {
		printf("Could not open audio: %s\n", SDL_GetError());
		return;
	}
	m_audioRate = f->sample_rate;
	m_audioChannels = f->channels;
}

void CSDLPlayer::unInitAudio()
{
	if (m_audio) {
		SDL_CloseAudioDevice(m_audio);
		m_audio = 0;
	}
	m_audioRate = m_audioChannels = 0;
	CAutoLock oLock(m_mutexAudio, "unInitAudio");
	while (!m_queueAudio.empty()) {
		SDemoAudioFrame* p = m_queueAudio.front();
		delete[] p->data;
		delete p;
		m_queueAudio.pop();
	}
}

void CSDLPlayer::sdlAudioCallback(void* userdata, Uint8* stream, int len)
{
	CSDLPlayer* self = (CSDLPlayer*)userdata;
	int need = len, pos = 0;
	memset(stream, 0, len);

	CAutoLock oLock(self->m_mutexAudio, "sdlAudioCallback");
	while (need > 0 && !self->m_queueAudio.empty()) {
		SDemoAudioFrame* p = self->m_queueAudio.front();
		int offset = p->dataTotal - p->dataLeft;
		int take = (int)min(p->dataLeft, (unsigned int)need);
		memcpy(stream + pos, p->data + offset, take);
		p->dataLeft -= take;
		need -= take;
		pos += take;
		if (p->dataLeft == 0) {
			self->m_queueAudio.pop();
			delete[] p->data;
			delete p;
		}
	}
}
