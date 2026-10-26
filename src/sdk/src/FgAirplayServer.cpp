#include "pch.h"
#include "FgAirplayServer.h"
#include <ws2tcpip.h>
#include <thread>
#include "CAutoLock.h"

#ifdef WIN32
#include   "iphlpapi.h"
#pragma   comment(lib,   "iphlpapi.lib   ")
#endif

BOOL GetMacAddress(char strMac[6]);

bool FgAirplayServer::parseAddress(const char* dotted, unsigned long* address)
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
	in_addr a;
	bool ok = inet_pton(AF_INET, dotted, &a) == 1;
	if (ok) *address = a.s_addr;
	WSACleanup();
	return ok;
}

// Can a socket be bound to this address, i.e. does an adapter carry it?
static bool addressIsLocal(unsigned long address)
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sockaddr_in sin = {};
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = address;
	bool ok = s != INVALID_SOCKET && bind(s, (sockaddr*)&sin, sizeof(sin)) == 0;
	if (s != INVALID_SOCKET) closesocket(s);
	WSACleanup();
	return ok;
}

FgAirplayServer::FgAirplayServer()
	: m_pDnsSd(NULL)
	, m_pAirplay(NULL)
	, m_pRaop(NULL)
	, m_sink(NULL)
	, m_audioSink(NULL)
{
	memset(&m_cb, 0, sizeof(m_cb));
	memset(&m_stAirplayCB, 0, sizeof(airplay_callbacks_t));
	memset(&m_stRaopCB, 0, sizeof(raop_callbacks_t));
	m_stAirplayCB.cls = this;
	m_stRaopCB.cls = this;

	m_stAirplayCB.video_play = ap_video_play;
	m_stAirplayCB.video_get_play_info = ap_video_get_play_info;

	m_stRaopCB.connected = connected;
	m_stRaopCB.disconnected = disconnected;
	m_stRaopCB.audio_set_volume = audio_set_volume;
	m_stRaopCB.audio_set_metadata = audio_set_metadata;
	m_stRaopCB.audio_set_coverart = audio_set_coverart;
	m_stRaopCB.audio_process = audio_process;
	m_stRaopCB.audio_flush = audio_flush;
	m_stRaopCB.video_process = video_process;
	m_stRaopCB.pin_show = pin_show;

	m_mutexMap = CreateMutex(NULL, FALSE, NULL);
}

FgAirplayServer::~FgAirplayServer()
{
	clearChannels();

	CloseHandle(m_mutexMap);
}

lm_error FgAirplayServer::start(const char* serverName, unsigned int raopPort, unsigned int airplayPort,
                                const lm_callbacks* callbacks, LmVideoSink* video, LmAudioSink* audio,
                                const FgServerOptions& options)
{
	m_cb = *callbacks;
	m_sink = video;
	m_audioSink = audio;

	unsigned short raop_port = raopPort;
	unsigned short airplay_port = airplayPort;
	char hwaddr[] = { 0x48, 0x5d, 0x60, 0x7c, 0xee, 0x22 };
	char* pemstr = NULL;

	int ret = 0;
	lm_error err = LM_OK;
	do {

		GetMacAddress(hwaddr);

		m_pAirplay = airplay_init(10, &m_stAirplayCB, pemstr, &ret);
		if (m_pAirplay == NULL) {
			err = LM_E_INTERNAL;
			break;
		}
		if (options.address && !addressIsLocal(options.address)) {
			err = LM_E_ADDRESS;
			break;
		}
		airplay_set_address(m_pAirplay, options.address);
		if (airplay_start(m_pAirplay, &airplay_port, hwaddr, sizeof(hwaddr), NULL) < 0) {
			err = LM_E_AIRPLAY_PORT;
			break;
		}
		airplay_set_log_level(m_pAirplay, RAOP_LOG_DEBUG);
		airplay_set_log_callback(m_pAirplay, &log_callback, this);

		m_pRaop = raop_init(10, &m_stRaopCB);
		if (m_pRaop == NULL) {
			err = LM_E_INTERNAL;
			break;
		}

		raop_set_log_level(m_pRaop, RAOP_LOG_DEBUG);
		raop_set_log_callback(m_pRaop, &log_callback, this);
		raop_set_identity(m_pRaop, serverName, hwaddr, sizeof(hwaddr));
		raop_set_pin(m_pRaop, options.pinMode, options.pin.c_str());
		raop_set_second_device(m_pRaop, options.replaceSecond);
		raop_set_address(m_pRaop, options.address);
		raop_set_display(m_pRaop, options.displayWidth, options.displayHeight, options.displayFps);
		if (raop_start(m_pRaop, &raop_port) < 0) {
			err = LM_E_RAOP_PORT;
			break;
		}
		raop_set_port(m_pRaop, raop_port);

		m_pDnsSd = dnssd_init(&ret);
		if (m_pDnsSd == NULL) {
			err = ret == DNSSD_ERROR_OUTOFMEM ? LM_E_NO_MEMORY : LM_E_MDNS_UNAVAILABLE;
			break;
		}
		dnssd_set_address(m_pDnsSd, options.address);
		int password = options.pinMode != 0;   // pw=true: the phone asks the user for the code
		if (dnssd_register_raop(m_pDnsSd, serverName, raop_port, hwaddr, sizeof(hwaddr), password) < 0 ||
		    dnssd_register_airplay(m_pDnsSd, serverName, airplay_port, hwaddr, sizeof(hwaddr), password) < 0 ||
		    dnssd_wait(m_pDnsSd) < 0) {
			err = LM_E_MDNS_REGISTER;
			break;
		}

		raop_log_info(m_pRaop, "Startup complete... Kill with Ctrl+C\n");
	} while (false);

	if (err != LM_OK) {
		stop();
	}

	return err;
}

void FgAirplayServer::stop()
{
	if (m_pDnsSd) {
		dnssd_unregister_airplay(m_pDnsSd);
		dnssd_unregister_raop(m_pDnsSd);
		dnssd_destroy(m_pDnsSd);
		m_pDnsSd = NULL;
	}

	if (m_pRaop) {
		raop_destroy(m_pRaop);
		m_pRaop = NULL;
	}

	if (m_pAirplay) {
		airplay_destroy(m_pAirplay);
		m_pAirplay = NULL;
	}

	clearChannels();
	memset(&m_cb, 0, sizeof(m_cb));
}

void FgAirplayServer::clearChannels()
{
	CAutoLock oLock(m_mutexMap, "clearChannels");
	while (m_mapChannel.size() > 0)
	{
		FgAirplayChannelMap::iterator it = m_mapChannel.begin();
		it->second->release();
		m_mapChannel.erase(it);
	}
}

FgAirplayChannel* FgAirplayServer::getChannel(const char* remoteDeviceId)
{
	FgAirplayChannelMap::iterator it = m_mapChannel.find(remoteDeviceId);
	return it == m_mapChannel.end() ? NULL : it->second;
}


void FgAirplayServer::connected(void* cls, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	CAutoLock oLock(pServer->m_mutexMap, "connected");
	FgAirplayChannel* pChannel = pServer->getChannel(remoteDeviceId);
	bool bFirstStream = (pChannel == NULL);
	if (bFirstStream)
	{
		pChannel = new FgAirplayChannel(&pServer->m_cb, pServer->m_sink);
		pServer->m_mapChannel[remoteDeviceId] = pChannel;
	}
	pChannel->m_nStreams++;

	if (bFirstStream && pServer->m_cb.on_connected)
	{
		pServer->m_cb.on_connected(pServer->m_cb.user, remoteDeviceId, remoteName);
	}
}

void FgAirplayServer::disconnected(void* cls, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	{
		CAutoLock oLock(pServer->m_mutexMap, "disconnected");
		FgAirplayChannelMap::iterator it = pServer->m_mapChannel.find(remoteDeviceId);
		if (it == pServer->m_mapChannel.end() || --it->second->m_nStreams > 0)
		{
			return;
		}
		it->second->release();
		pServer->m_mapChannel.erase(it);
	}

	if (pServer->m_cb.on_disconnected)
	{
		pServer->m_cb.on_disconnected(pServer->m_cb.user, remoteDeviceId, remoteName);
	}
}

void FgAirplayServer::audio_set_volume(void* cls, void* session, float volume, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_cb.on_volume)
	{
		pServer->m_cb.on_volume(pServer->m_cb.user, remoteDeviceId, volume);
	}
}

void FgAirplayServer::audio_set_metadata(void* cls, void* session, const void* buffer, int buflen, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_cb.on_metadata)
	{
		pServer->m_cb.on_metadata(pServer->m_cb.user, remoteDeviceId, buffer, (size_t)buflen);
	}
}

void FgAirplayServer::audio_set_coverart(void* cls, void* session, const void* buffer, int buflen, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_cb.on_cover_art)
	{
		pServer->m_cb.on_cover_art(pServer->m_cb.user, remoteDeviceId, buffer, (size_t)buflen);
	}
}

void FgAirplayServer::audio_process(void* cls, pcm_data_struct* data, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer || (!pServer->m_audioSink && !pServer->m_cb.on_audio))
	{
		return;
	}
	// The core's buffer stays valid for the whole callback; no copy needed.
	lm_audio_frame frame;
	frame.pts = data->pts;
	frame.sample_rate = data->sample_rate;
	frame.channels = data->channels;
	frame.bits_per_sample = data->bits_per_sample;
	frame.data = (const uint8_t*)data->data;   // 16-bit PCM, size in bytes
	frame.size = data->data_len;
	if (pServer->m_audioSink)
	{
		pServer->m_audioSink->play(&frame);
		return;
	}
	pServer->m_cb.on_audio(pServer->m_cb.user, remoteDeviceId, &frame);
}

void FgAirplayServer::audio_flush(void* cls, void* session, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_audioSink)
	{
		pServer->m_audioSink->flush();
	}
}

void FgAirplayServer::video_process(void* cls, h264_decode_struct* h264data, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (h264data->data_len <= 0)
	{
		return;
	}

	SFgH264Data packet;
	memset(&packet, 0, sizeof(packet));
	packet.size = h264data->data_len;
	packet.data = h264data->data;
	packet.is_key = h264data->frame_type == 0;   // frame_type 0: SPS/PPS, 1: a frame
	packet.pts = h264data->pts;

	FgAirplayChannel* pChannel = NULL;
	{
		CAutoLock oLock(pServer->m_mutexMap, "video_process");
		pChannel = pServer->getChannel(remoteDeviceId);
		if (pChannel) {
			pChannel->addRef();
		}
	}
	if (pChannel)
	{
		pChannel->decodeH264Data(&packet, remoteName, remoteDeviceId);
		pChannel->release();
	}
}

void FgAirplayServer::ap_video_play(void* cls, char* url, double volume, double start_pos)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_cb.on_play_url)
	{
		pServer->m_cb.on_play_url(pServer->m_cb.user, url, (float)volume, start_pos);
	}
}

void FgAirplayServer::ap_video_get_play_info(void* cls, double* duration, double* position, double* rate)
{
	*duration = *position = *rate = 0;   // URL playback is up to the shell; nothing to report
}

void FgAirplayServer::pin_show(void* cls, const char* pin)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_cb.on_pin)
	{
		pServer->m_cb.on_pin(pServer->m_cb.user, pin);
	}
}

void FgAirplayServer::log_callback(void* cls, int level, const char* msg)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_cb.on_log)
	{
		pServer->m_cb.on_log(pServer->m_cb.user, (lm_log_level)level, msg);
	}
}

BOOL GetMacAddress(char strMac[6])
{
	// The MAC is our identity towards iOS. Take it from the adapter that
	// routes to the internet: VPN and Hyper-V adapters change the enumeration
	// order between runs and would make the phone see a new receiver each time.
	DWORD best = (DWORD)-1;
	GetBestInterface(0x08080808, &best);   // 8.8.8.8, same bytes in either byte order

	ULONG size = 0;
	if (GetAdaptersInfo(NULL, &size) != ERROR_BUFFER_OVERFLOW) {
		return FALSE;
	}
	PIP_ADAPTER_INFO list = (PIP_ADAPTER_INFO)malloc(size);
	if (list == NULL || GetAdaptersInfo(list, &size) != NO_ERROR) {
		free(list);
		return FALSE;
	}

	PIP_ADAPTER_INFO pick = list;   // no default route: fall back to the first adapter
	for (PIP_ADAPTER_INFO a = list; a != NULL; a = a->Next) {
		if (a->Index == best) {
			pick = a;
			break;
		}
	}
	memcpy(strMac, pick->Address, 6);
	free(list);
	return TRUE;
}
