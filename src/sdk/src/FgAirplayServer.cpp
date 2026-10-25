#include "pch.h"
#include "FgAirplayServer.h"
#include "Airplay2Head.h"
#include <thread>
#include "CAutoLock.h"

#ifdef WIN32
#include   "iphlpapi.h"  
#pragma   comment(lib,   "iphlpapi.lib   ")  
#endif

BOOL GetMacAddress(char strMac[6]);

FgAirplayServer::FgAirplayServer()
	: m_pCallback(NULL)
	, m_pDnsSd(NULL)
	, m_pAirplay(NULL)
	, m_pRaop(NULL)
	, m_fScaleRatio(1.0f)
{
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

	m_mutexMap = CreateMutex(NULL, FALSE, NULL);
}

FgAirplayServer::~FgAirplayServer()
{
	m_pCallback = NULL;

	clearChannels();

	CloseHandle(m_mutexMap);
}

lm_error FgAirplayServer::start(const char serverName[AIRPLAY_NAME_LEN],
	unsigned int raopPort, unsigned int airplayPort,
	IAirServerCallback* callback)
{
	m_pCallback = callback;

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
		if (raop_start(m_pRaop, &raop_port) < 0) {
			err = LM_E_RAOP_PORT;
			break;
		}
		raop_set_port(m_pRaop, raop_port);

		m_pDnsSd = dnssd_init(&ret);
		if (m_pDnsSd == NULL) {
			err = ret == DNSSD_ERROR_OUTOFMEM ? LM_E_NO_MEMORY : LM_E_BONJOUR_MISSING;
			break;
		}
		if (dnssd_register_raop(m_pDnsSd, serverName, raop_port, hwaddr, sizeof(hwaddr), 0) < 0 ||
		    dnssd_register_airplay(m_pDnsSd, serverName, airplay_port, hwaddr, sizeof(hwaddr)) < 0) {
			err = LM_E_BONJOUR_REGISTER;
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
	m_pCallback = NULL;
}

float FgAirplayServer::setScale(float fRatio)
{
	m_fScaleRatio = min(10, max(0.1, fRatio));

	CAutoLock oLock(m_mutexMap, "setScale");
	FgAirplayChannelMap::iterator it;
	for (it = m_mapChannel.begin(); it != m_mapChannel.end(); ++it)
	{
		it->second->setScale(m_fScaleRatio);
	}
	return m_fScaleRatio;
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
		pChannel = new FgAirplayChannel(pServer->m_pCallback);
		pServer->m_mapChannel[remoteDeviceId] = pChannel;
	}
	pChannel->m_nStreams++;

	if (bFirstStream && pServer->m_pCallback != NULL)
	{
		pServer->m_pCallback->connected(remoteName, remoteDeviceId);
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

	if (pServer->m_pCallback != NULL)
	{
		pServer->m_pCallback->disconnected(remoteName, remoteDeviceId);
	}
}

void FgAirplayServer::audio_set_volume(void* cls, void* session, float volume, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_pCallback)
	{
		pServer->m_pCallback->audioSetVolume(volume, remoteName, remoteDeviceId);
	}
}

void FgAirplayServer::audio_set_metadata(void* cls, void* session, const void* buffer, int buflen, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_pCallback)
	{
		pServer->m_pCallback->audioSetMetadata(buffer, buflen, remoteName, remoteDeviceId);
	}
}

void FgAirplayServer::audio_set_coverart(void* cls, void* session, const void* buffer, int buflen, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (pServer && pServer->m_pCallback)
	{
		pServer->m_pCallback->audioSetCoverArt(buffer, buflen, remoteName, remoteDeviceId);
	}
}

void FgAirplayServer::audio_process(void* cls, pcm_data_struct* data, const char* remoteName, const char* remoteDeviceId)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}

	if (pServer->m_pCallback != NULL)
	{
		SFgAudioFrame* frame = new SFgAudioFrame();
		frame->bitsPerSample = data->bits_per_sample;
		frame->channels = data->channels;
		frame->pts = data->pts;
		frame->sampleRate = data->sample_rate;
		frame->dataLen = data->data_len;
		frame->data = new uint8_t[frame->dataLen];
		memcpy(frame->data, data->data, frame->dataLen);

		pServer->m_pCallback->outputAudio(frame, remoteName, remoteDeviceId);
		delete[] frame->data;
		delete frame;
	}
}

void FgAirplayServer::audio_flush(void* cls, void* session, const char* remoteName, const char* remoteDeviceId)
{
}

void FgAirplayServer::audio_destroy(void* cls, void* session, const char* remoteName, const char* remoteDeviceId)
{
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

	SFgH264Data* pData = new SFgH264Data();
	memset(pData, 0, sizeof(SFgH264Data));

	if (h264data->frame_type == 0)
	{
		pData->size = h264data->data_len;
		pData->data = new uint8_t[pData->size];
		pData->is_key = 1;
		memcpy(pData->data, h264data->data, h264data->data_len);
	}
	else if (h264data->frame_type == 1)
	{
		pData->size = h264data->data_len;
		pData->data = new uint8_t[pData->size];
		memcpy(pData->data, h264data->data, h264data->data_len);
	}

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
		pChannel->decodeH264Data(pData, remoteName, remoteDeviceId);
		pChannel->release();
	}
	delete[] pData->data;
	delete pData;
}

void FgAirplayServer::ap_video_play(void* cls, char* url, double volume, double start_pos)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (pServer->m_pCallback)
	{
		pServer->m_pCallback->videoPlay(url, volume, start_pos);
	}
}

void FgAirplayServer::ap_video_get_play_info(void* cls, double* duration, double* position, double* rate)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (pServer->m_pCallback)
	{
		pServer->m_pCallback->videoGetPlayInfo(duration, position, rate);
	}
}

void FgAirplayServer::log_callback(void* cls, int level, const char* msg)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer) 
	{
		return;
	}
	if (pServer->m_pCallback)
	{
		pServer->m_pCallback->log(level, msg);
	}
}

BOOL GetMacAddress(char strMac[6])
{
	PIP_ADAPTER_INFO pAdapterInfo;
	DWORD AdapterInfoSize;
	DWORD Err;
	AdapterInfoSize = 0;
	Err = GetAdaptersInfo(NULL, &AdapterInfoSize);
	if ((Err != 0) && (Err != ERROR_BUFFER_OVERFLOW)) {
		printf("GetAdaptersInfo failed to query buffer size\n");
		return   FALSE;
	}
	pAdapterInfo = (PIP_ADAPTER_INFO)GlobalAlloc(GPTR, AdapterInfoSize);
	if (pAdapterInfo == NULL) {
		printf("GlobalAlloc for adapter info failed\n");
		return   FALSE;
	}
	if (GetAdaptersInfo(pAdapterInfo, &AdapterInfoSize) != 0) {
		printf("GetAdaptersInfo failed\n");
		GlobalFree(pAdapterInfo);
		return   FALSE;
	}
	for (int i = 0; i < 6; i++)
	{
		strMac[i] = pAdapterInfo->Address[i];
	}

	GlobalFree(pAdapterInfo);
	return   TRUE;
}