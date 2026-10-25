#pragma once
#include "litemirror.h"
#include "stream.h"
#include <map>
#include <string>

#include "dnssd.h"
#include "airplay.h"
#include "raop.h"
#include "FgAirplayChannel.h"
#include "LmAudioSink.h"

typedef std::map<std::string, FgAirplayChannel*> FgAirplayChannelMap;

// Owns the protocol core (RTSP, HTTP, mDNS) and one FgAirplayChannel per
// connected device. Everything it reports goes out through lm_callbacks.
class FgAirplayServer
{
public:
	FgAirplayServer();
	virtual ~FgAirplayServer();

	lm_error start(const char* serverName, unsigned int raopPort, unsigned int airplayPort,
	               const lm_callbacks* callbacks, LmVideoSink* video, LmAudioSink* audio);
	void stop();

protected:
	void clearChannels();
	FgAirplayChannel* getChannel(const char* remoteDeviceId);

	static void connected(void* cls, const char* remoteName, const char* remoteDeviceId);
	static void disconnected(void* cls, const char* remoteName, const char* remoteDeviceId);
	static void audio_set_volume(void* cls, void* session, float volume, const char* remoteName, const char* remoteDeviceId);
	static void audio_set_metadata(void* cls, void* session, const void* buffer, int buflen, const char* remoteName, const char* remoteDeviceId);
	static void audio_set_coverart(void* cls, void* session, const void* buffer, int buflen, const char* remoteName, const char* remoteDeviceId);
	static void audio_process(void* cls, pcm_data_struct* data, const char* remoteName, const char* remoteDeviceId);
	static void audio_flush(void* cls, void* session, const char* remoteName, const char* remoteDeviceId);
	static void video_process(void* cls, h264_decode_struct* data, const char* remoteName, const char* remoteDeviceId);
	static void log_callback(void* cls, int level, const char* msg);

	static void ap_video_play(void* cls, char* url, double volume, double start_pos);
	static void ap_video_get_play_info(void* cls, double* duration, double* position, double* rate);

protected:
	lm_callbacks			m_cb;
	LmVideoSink*			m_sink;        // NULL: frames go to m_cb.on_video
	LmAudioSink*			m_audioSink;   // NULL: PCM goes to m_cb.on_audio

	dnssd_t*				m_pDnsSd;
	airplay_t*				m_pAirplay;
	raop_t*					m_pRaop;

	airplay_callbacks_t		m_stAirplayCB;
	raop_callbacks_t		m_stRaopCB;

	void*					m_mutexMap;

	FgAirplayChannelMap		m_mapChannel;
};
