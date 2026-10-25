#include "FgAirplayChannel.h"
#include "CAutoLock.h"

FgAirplayChannel::FgAirplayChannel(IAirServerCallback* pCallback)
: m_nRef(1)
, m_nStreams(0)
, m_pCallback(pCallback)
, m_bDecoderOpen(false)
{
	m_mutexVideo = CreateMutex(NULL, FALSE, NULL);
}

FgAirplayChannel::~FgAirplayChannel()
{
	m_pCallback = NULL;
	{
		CAutoLock oLock(m_mutexVideo, "~FgAirplayChannel");
		m_decoder.close();
	}
	CloseHandle(m_mutexVideo);
}

long FgAirplayChannel::addRef()
{
	long lRef = InterlockedIncrement(&m_nRef);
	return (m_nRef > 1 ? m_nRef : 1);
}

long FgAirplayChannel::release()
{
	LONG lRef = InterlockedDecrement(&m_nRef);
	if (0 == lRef)
	{
		delete this;
		return 0;
	}
	return (m_nRef > 1 ? m_nRef : 1);
}

int FgAirplayChannel::decodeH264Data(SFgH264Data* data, const char* remoteName, const char* remoteDeviceId)
{
	CAutoLock oLock(m_mutexVideo, "decodeH264Data");

	if (!m_bDecoderOpen) {
		if (!data->is_key) {
			return 0;   // cannot start mid-stream without SPS/PPS
		}
		m_bDecoderOpen = m_decoder.open();
		if (!m_bDecoderOpen) {
			return -1;
		}
	}

	if (!m_decoder.decode(data->data, (size_t)data->size, (uint64_t)data->pts)) {
		return -1;
	}
	while (m_decoder.receive()) {
		const MfH264Decoder::Frame& f = m_decoder.frame();
		if (m_pCallback == NULL) {
			continue;
		}
		// NV12 straight from the decoder: Y plane, then interleaved UV.
		// dataLen[0] is the byte offset of the UV plane, not width*height.
		SFgVideoFrame frame;
		memset(&frame, 0, sizeof(frame));
		frame.pts = f.pts;
		frame.isKey = f.key;
		frame.width = f.width;
		frame.height = f.height;
		frame.data = (unsigned char*)f.plane[0];
		frame.pitch[0] = f.pitch[0];
		frame.pitch[1] = f.pitch[1];
		frame.dataLen[0] = (unsigned int)(f.plane[1] - f.plane[0]);
		frame.dataLen[1] = f.pitch[1] * ((f.height + 1) / 2);
		frame.dataTotalLen = frame.dataLen[0] + frame.dataLen[1];
		m_pCallback->outputVideo(&frame, remoteName, remoteDeviceId);
	}
	return 0;
}
