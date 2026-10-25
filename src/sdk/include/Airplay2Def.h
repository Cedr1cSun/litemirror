#pragma once

#define AIRPLAY_NAME_LEN 128

typedef struct SFgAudioFrame {
	unsigned long long pts;
	unsigned int sampleRate;
	unsigned short channels;
	unsigned short bitsPerSample;
	unsigned int dataLen;
	unsigned char* data;
} SFgAudioFrame;

// Decoded video frame, NV12: data holds the Y plane, the interleaved UV
// plane starts at data + dataLen[0]; pitch[0] and pitch[1] apply.
typedef struct SFgVideoFrame {
	unsigned long long pts;
	int isKey;
	unsigned int width;
	unsigned int height;
	unsigned int pitch[3];
	unsigned int dataLen[3];
	unsigned int dataTotalLen;
	unsigned char* data;
}SFgVideoFrame;
