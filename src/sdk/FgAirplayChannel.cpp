#include "FgAirplayChannel.h"
#include "CAutoLock.h"

FgAirplayChannel::FgAirplayChannel(const lm_callbacks* callbacks, LmVideoSink* sink)
: m_nRef(1)
, m_nStreams(0)
, m_cb(callbacks)
, m_sink(sink)
, m_bDecoderOpen(false)
{
	m_mutexVideo = CreateMutex(NULL, FALSE, NULL);
}

FgAirplayChannel::~FgAirplayChannel()
{
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
		m_bDecoderOpen = m_decoder.open(m_sink ? m_sink->device() : NULL);
		if (!m_bDecoderOpen) {
			return -1;
		}
	}

	if (!m_decoder.decode(data->data, (size_t)data->size, (uint64_t)data->pts)) {
		return -1;
	}
	while (m_decoder.receive()) {
		const MfH264Decoder::Frame& f = m_decoder.frame();
		if (m_sink) {
			// shortcut: nothing is drawn if the MFT declined the sink's device
			// and decoded in software; add an NV12 upload path if that shows up
			if (m_sink->draw(f.texture, f.subresource, f.width, f.height)) m_sink->present();
			continue;
		}
		if (m_cb->on_video == NULL) {
			continue;
		}
		// NV12 straight from the decoder's buffer, no copy
		lm_video_frame frame;
		frame.pts = f.pts;
		frame.is_key = f.key;
		frame.width = f.width;
		frame.height = f.height;
		frame.plane[0] = f.plane[0];
		frame.plane[1] = f.plane[1];
		frame.pitch[0] = f.pitch[0];
		frame.pitch[1] = f.pitch[1];
		m_cb->on_video(m_cb->user, remoteDeviceId, &frame);
	}
	return 0;
}
