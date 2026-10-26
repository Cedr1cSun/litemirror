#pragma once
#include "litemirror.h"
#include "MfH264Decoder.h"
#include "LmVideoSink.h"

// H264 data for decoding
typedef struct SFgH264Data {
	uint64_t pts;
	int size;
	int is_key;
	int width;
	int height;
	unsigned char* data;
}SFgH264Data;


// One connected device: decodes its mirror stream and reports frames, or
// draws them straight into the window sink when the shell set one.
class FgAirplayChannel
{
public:
	// callbacks and sink must outlive the channel; FgAirplayServer owns both.
	// hardware=false decodes on the CPU.
	explicit FgAirplayChannel(const lm_callbacks* callbacks, LmVideoSink* sink = NULL, bool hardware = true);
	~FgAirplayChannel();

public:
	long addRef();
	long release();

	// Live streams (audio, mirror) from this device; FgAirplayServer updates it under its map lock.
	int						m_nStreams;

	// Packets arrive as Annex B: the first one (is_key) carries SPS and PPS,
	// the rest one frame each. Anything before the first key packet is dropped.
	int decodeH264Data(SFgH264Data* data, const char* remoteName, const char* remoteDeviceId);

protected:
	long m_nRef;

	const lm_callbacks*		m_cb;
	LmVideoSink*			m_sink;
	MfH264Decoder			m_decoder;
	bool					m_hardware;
	bool					m_bDecoderOpen;
	uint32_t				m_reportedW, m_reportedH;   // last size given to on_video_size
	void*					m_mutexVideo;
};
