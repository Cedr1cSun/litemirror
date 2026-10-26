#include "FgAirplayChannel.h"
#include "CAutoLock.h"

FgAirplayChannel::FgAirplayChannel(const lm_callbacks* callbacks, LmVideoSink* sink, bool hardware, bool lowLatency)
: m_nRef(1)
, m_nStreams(0)
, m_cb(callbacks)
, m_sink(sink)
, m_hardware(hardware)
, m_lowLatency(lowLatency)
, m_bDecoderOpen(false)
, m_reportedW(0)
, m_reportedH(0)
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
		m_bDecoderOpen = m_decoder.open(m_sink ? m_sink->device() : NULL, m_hardware, m_lowLatency);
		if (!m_bDecoderOpen) {
			return -1;
		}
	}

	if (!m_decoder.decode(data->data, (size_t)data->size, (uint64_t)data->pts)) {
		return -1;
	}
	while (m_decoder.receive()) {
		const MfH264Decoder::Frame& f = m_decoder.frame();
		if ((f.width != m_reportedW || f.height != m_reportedH) && m_cb->on_video_size) {
			m_reportedW = f.width;
			m_reportedH = f.height;
			m_cb->on_video_size(m_cb->user, remoteDeviceId, f.width, f.height);
		}
		if (m_sink) {
			// a GPU picture is drawn in place; a CPU one (software decoding) is uploaded first
			bool drawn = f.texture ? m_sink->draw(f.texture, f.subresource, f.width, f.height)
			                       : m_sink->draw(f.plane[0], f.pitch[0], f.plane[1], f.pitch[1], f.width, f.height);
			if (drawn) m_sink->present();
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
