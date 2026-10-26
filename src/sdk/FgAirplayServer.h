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
#include "LmVideoSink.h"

typedef std::map<std::string, FgAirplayChannel*> FgAirplayChannelMap;

// What the shell set through the lm_server_set_* calls before starting
struct FgServerOptions {
	FgServerOptions() : pinMode(0), secondDevice(0), address(0), displayWidth(1920), displayHeight(1080), displayFps(60), hardwareDecode(true), lowLatency(true), followVolume(true), scaler(0) {}
	int pinMode;          // lm_pin_mode
	std::string pin;      // LM_PIN_FIXED
	int secondDevice;     // lm_second_device
	unsigned long address;   // IPv4 to listen and announce on, network byte order; 0 = all
	unsigned int displayWidth, displayHeight, displayFps;   // what the phone encodes at
	bool hardwareDecode;  // false: decode on the CPU
	bool lowLatency;      // false: reorder queue in the decoder and vsync on present
	LmColour colour;      // picture adjustments, kept so a later set_window gets them
	std::wstring audioDevice;   // output device id, "" = default
	bool followVolume;    // the phone's volume buttons set this app's session volume
	int scaler;           // 0 video processor, 1 bicubic shader
};

// Owns the protocol core (RTSP, HTTP, mDNS) and one FgAirplayChannel per
// connected device. Everything it reports goes out through lm_callbacks.
class FgAirplayServer
{
public:
	FgAirplayServer();
	virtual ~FgAirplayServer();

	lm_error start(const char* serverName, unsigned int raopPort, unsigned int airplayPort,
	               const lm_callbacks* callbacks, LmVideoSink* video, LmAudioSink* audio,
	               const FgServerOptions& options);
	void stop();

	// Dotted IPv4 to network byte order; false when it is not one
	static bool parseAddress(const char* dotted, unsigned long* address);

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
	static void pin_show(void* cls, const char* pin);
	static int admit(void* cls, const char* remoteName, const char* remoteDeviceId);

	static void ap_video_play(void* cls, char* url, double volume, double start_pos);
	static void ap_video_get_play_info(void* cls, double* duration, double* position, double* rate);

protected:
	lm_callbacks			m_cb;
	FgServerOptions			m_options;
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
