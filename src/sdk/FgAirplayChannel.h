#pragma once
#include "litemirror.h"
#include "MfH264Decoder.h"

// H264 data for decoding
typedef struct SFgH264Data {
	uint64_t pts;
	int size;
	int is_key;
	int width;
	int height;
	unsigned char* data;
}SFgH264Data;


// One connected device: decodes its mirror stream and reports frames.
class FgAirplayChannel
{
public:
	// callbacks must outlive the channel; FgAirplayServer owns both
	explicit FgAirplayChannel(const lm_callbacks* callbacks);
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
	MfH264Decoder			m_decoder;
	bool					m_bDecoderOpen;
	void*					m_mutexVideo;
};
