#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <RGB.H>

#include <obs.h>
#include <audio.h>

#include <mutex>
#include <array>

#define PROP_INPUT_ID "input"
#define PROP_INTERNAL_WIDTH "internal_width"
#define PROP_INTERNAL_HEIGHT "internal_height"

enum class SignalState
{
	Active,
	Inactive,
	Invalid
};

enum class PixelFormat
{
	RGB32,
	RGB24,
	RGB16,
	YUY2,
	NV12, // unused for now
	Y8
};

const struct
{
	COLORREF Mask[3];
} pixFmtMask[] =
{ { 0x00ff0000, 0x0000ff00, 0x000000ff, },
{ 0x00ff0000, 0x0000ff00, 0x000000ff, },
{ 0x0000f800, 0x000007e0, 0x0000001f, },
{ 0x00000000, 0x00000000, 0x00000000, },
{ 0x00000000, 0x00000000, 0x00000000, },
{ 0x00000000, 0x00000000, 0x00000000, },
};

const int pixFmtBpp[] = { 32, 24, 16, 16, 12, 8 };

const DWORD pixFmtFCC[] = { BI_BITFIELDS, BI_BITFIELDS, BI_BITFIELDS, '2YUY', '21VN', 'YERG' };

struct CapturedFrame
{
	CapturedFrame(int width, int height, PixelFormat pixelFormat);

	obs_source_frame obsFrame;	
	BITMAPINFO bitmapInfo;
	std::vector<uint8_t> buffer;

	unsigned int updateCount;

	void chainOutputBuffer(HRGB hRGB);
};

struct CapturedAudio
{
	static const int AUDIO_BUFFER_LENGTH = 480;

	CapturedAudio();

	obs_source_audio data;
	std::array<uint8_t, AUDIO_BUFFER_LENGTH> buffer;
};

class VisionSource
{
private:
	static const int NUM_BUFFERS = 3;

	HRGB hRGB;
	HAUDIO hAudio;

	SignalState signal;
	bool capturing;

	std::mutex mutex;

	std::vector<std::unique_ptr<CapturedFrame>> frames;
	std::vector<std::unique_ptr<CapturedAudio>> audioBuffers;

	int inputId;

	int internalWidth;
	int internalHeight;

	//unsigned int cropX;
	//unsigned int cropY;

	obs_source_t* source;

public:
	VisionSource(obs_source_t* source, obs_data_t* settings);
	VisionSource(VisionSource&&) = default;
	VisionSource& operator=(const VisionSource&) & = default;
	VisionSource& operator=(VisionSource&&) & = default;
	~VisionSource();

	bool startVideoCapture(const int input);
	bool startAudioCapture(const int input);

	void start();
	void stop();

	static RGBFRAMECAPTUREDFNEX Receive;
	static RGBMODECHANGEDFN ResolutionSwitch;
	static RGBNOSIGNALFN NoSignal;
	static RGBINVALIDSIGNALFN InvalidSignal;

	static AUDIOCAPTUREDFN AudioReceive;

	const char* getName();

	unsigned int getWidth();
	unsigned int getHeight();

	void activate();
	void deactivate();

	static std::string getModeText(unsigned long input);

	void update(obs_data_t* settings);

	static bool inputSettingModified(obs_properties_t* prop, obs_property_t* p, obs_data_t* settings);

	static void getDefaults(obs_data* s);
	static obs_properties* getProperties();
	static obs_source_info getSourceInfo();
};
