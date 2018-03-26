#include "VisionSource.hpp"

#include <obs.h>
#include <util/platform.h>

#include <RGB.H>
#include <RGBAPI.H>
#include <RGBERROR.H>

#include <AUDIOAPI.H>
#include <string>

CapturedFrame::CapturedFrame(const int width, const int height, PixelFormat pixelFormat)
	: obsFrame(), bitmapInfo()
{
	const auto bpp = pixFmtBpp[int(pixelFormat)];
	const auto cformat = pixFmtFCC[int(pixelFormat)];
	const auto imageSize = width * height * bpp / 8;

	auto mask = pixFmtMask[int(pixelFormat)];

	buffer.resize(imageSize);

	bitmapInfo.bmiHeader.biWidth = width;
	bitmapInfo.bmiHeader.biHeight = -height;
	bitmapInfo.bmiHeader.biBitCount = bpp;
	bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapInfo.bmiHeader.biPlanes = 1;
	bitmapInfo.bmiHeader.biCompression = cformat;
	bitmapInfo.bmiHeader.biXPelsPerMeter = 3000;
	bitmapInfo.bmiHeader.biYPelsPerMeter = 3000;
	bitmapInfo.bmiHeader.biClrUsed = 0;
	bitmapInfo.bmiHeader.biClrImportant = 0;
	bitmapInfo.bmiHeader.biSizeImage = imageSize;

	memcpy(&bitmapInfo.bmiColors, &mask, sizeof(mask));

	obsFrame.data[0] = buffer.data();
	obsFrame.linesize[0] = width * bpp / 8;
	obsFrame.width = width;
	obsFrame.height = height;
	obsFrame.format = VIDEO_FORMAT_BGRX;
	obsFrame.flip = false;

	updateCount = 0;
}

void CapturedFrame::chainOutputBuffer(HRGB hRGB)
{
	const auto err = RGBChainOutputBuffer(hRGB, &bitmapInfo, buffer.data());
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not set Chain Output Buffer 0x%08lX", err);
	}
}

CapturedAudio::CapturedAudio()
	: data(), buffer()
{
	data.data[0] = buffer.data();
	data.frames = 480;
	data.speakers = SPEAKERS_MONO;
	data.samples_per_sec = 48000;
	data.format = AUDIO_FORMAT_U8BIT;
}

VisionSource::VisionSource(obs_source_t* source, obs_data_t* settings)
	: hRGB(0),
	  hAudio(0),
	  signal(SignalState::Invalid), 
	  capturing(false), 
	  inputId(0), 
	  internalWidth(640), 
	  internalHeight(480),
	  source(source)
{
	inputId = obs_data_get_int(settings, PROP_INPUT_ID);
	internalWidth = obs_data_get_int(settings, PROP_INTERNAL_WIDTH);
	internalHeight = obs_data_get_int(settings, PROP_INTERNAL_HEIGHT);

	blog(LOG_DEBUG, "Create!");
}

VisionSource::~VisionSource()
{
	stop();
}

bool VisionSource::startVideoCapture(const int input)
{
	SIGNALTYPE signalType;
	unsigned long dummy;

	RGBGetInputSignalType(input, &signalType, &dummy, &dummy, &dummy);

	switch (signalType)
	{
	case RGB_SIGNALTYPE_NOSIGNAL:
		{
			signal = SignalState::Inactive;
			break;
		}
	case RGB_SIGNALTYPE_OUTOFRANGE:
		{
			signal = SignalState::Invalid;
			break;
		}
	default:
		signal = SignalState::Active;
	}

	for (int i = 0; i < NUM_BUFFERS; i++)
	{
		frames.push_back(std::make_unique<CapturedFrame>(internalWidth, internalHeight, PixelFormat::RGB32));
	}

	auto err = RGBOpenInput(input, &hRGB);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not open input 0x%08lX", err);
		return true;
	}

	err = RGBSetFrameDropping(hRGB, 0);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not set frame dropping value 0x%08lX", err);
		return true;
	}

	// TODO: Cropping settings
	RGBEnableCropping(hRGB, 0);

	err = RGBSetFrameCapturedFnEx(hRGB, &Receive, (ULONG_PTR)this);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not set Receive callback 0x%08lX", err);
		return true;
	}

	for (auto&& frame : frames)
	{
		frame->chainOutputBuffer(hRGB);
	}

	err = RGBSetModeChangedFn(hRGB, &ResolutionSwitch, (ULONG_PTR)this);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not set ResolutionSwitch callback 0x%08lX", err);
		return true;
	}

	err = RGBSetNoSignalFn(hRGB, &NoSignal, (ULONG_PTR)this);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not set NoSignal callback 0x%08lX", err);
		return true;
	}

	err = RGBSetInvalidSignalFn(hRGB, &InvalidSignal, (ULONG_PTR)this);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not set InvalidSignal callback 0x%08lX", err);
		return true;
	}

	err = RGBUseOutputBuffers(hRGB, true);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not set UseOutputBuffers 0x%08lX", err);
		return true;
	}

	err = RGBStartCapture(hRGB);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not start capture 0x%08lX", err);
		return true;
	}

	capturing = true;
	return false;
}

bool VisionSource::startAudioCapture(const int input)
{
	// Audio stuff
	auto err = RGBAudioOpenInput(AudioReceive, (ULONG_PTR)this, input, &hAudio);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not open audio input 0x%08lX", err);
		return false;
	}

	unsigned long audioFormatCount;
	err = RGBAudioGetCapabilitiesCount(input, &audioFormatCount);
	if (err != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not get audio formats 0x%08lX", err);
		return false;
	}

	std::vector<PAUDIOCAPS> audioFormats;
	for (unsigned long i = 0; i < audioFormatCount; i++)
	{
		PAUDIOCAPS format = nullptr;
		err = RGBAudioGetCapabilities(input, i, format);
		if (err != RGBERROR_NO_ERROR)
		{
			blog(LOG_ERROR, "Could not get audio formats 0x%08lX", err);
			return false;
		}

		audioFormats.push_back(format);
	}

	return true;
}

void VisionSource::start()
{
	std::scoped_lock<std::mutex> lock(mutex);

	const int input = 0;

	if (!startVideoCapture(input)) return;
	if (!startAudioCapture(input)) return;
}

void VisionSource::stop()
{
	if (!capturing)
	{
		return;
	}

	if (RGBUseOutputBuffers(hRGB, false) == RGBERROR_NO_ERROR)
	{
		capturing = false;
	}

	RGBStopCapture(hRGB);
	RGBCloseInput(hRGB);

	for (auto&& frame : frames)
		frame.release();
	frames.clear();

	RGBAudioCloseInput(hAudio);

	for (auto&& audio : audioBuffers)
		audio.release();
	audioBuffers.clear();

	hRGB = 0;
	hAudio = 0;
}

void RGBCBKAPI VisionSource::Receive(HWND hWnd, HRGB hRGB, PRGBFRAMEDATA pFrameData, ULONG_PTR userData)
{
	auto sharedInfo = reinterpret_cast<VisionSource*>(userData);

	std::scoped_lock<std::mutex> lock(sharedInfo->mutex);

	if (sharedInfo->capturing)
	{
		for (auto&& frame : sharedInfo->frames)
		{
			if (frame->buffer.data() == pFrameData->PBitmapBits)
			{
				frame->updateCount++;

				frame->obsFrame.timestamp = os_gettime_ns();
				obs_source_output_video(sharedInfo->source, &frame->obsFrame);

				frame->chainOutputBuffer(hRGB);

				break;
			}
		}
	}
}

void RGBCBKAPI VisionSource::ResolutionSwitch(HWND hWnd, HRGB hRGB, PRGBMODECHANGEDINFO pModeChangedInfo, ULONG_PTR userData)
{
	auto sharedInfo = reinterpret_cast<VisionSource*>(userData);

	{
		std::scoped_lock<std::mutex> lock(sharedInfo->mutex);
		sharedInfo->signal = SignalState::Active;
	}

	unsigned long width;
	unsigned long height;
	if (RGBGetCaptureWidthDefault(hRGB, &width)   == RGBERROR_NO_ERROR && 
		RGBGetCaptureHeightDefault(hRGB, &height) == RGBERROR_NO_ERROR)
	{
		RGBSetCaptureWidth(hRGB, width);
		RGBSetCaptureHeight(hRGB, height);
		// TODO: set renderCX/CY ?
	}

}

void RGBCBKAPI VisionSource::NoSignal(HWND hWnd, HRGB hRGB, ULONG_PTR userData)
{
	auto sharedInfo = reinterpret_cast<VisionSource*>(userData);
	std::scoped_lock<std::mutex> lock(sharedInfo->mutex);

	sharedInfo->signal = SignalState::Inactive;
}

void RGBCBKAPI VisionSource::InvalidSignal(HWND hWnd, HRGB hRGB, unsigned long horClock, unsigned long verClock, ULONG_PTR userData)
{
	auto sharedInfo = reinterpret_cast<VisionSource*>(userData);
	std::scoped_lock<std::mutex> lock(sharedInfo->mutex);

	sharedInfo->signal = SignalState::Invalid;
}

void AUDIOCBKAPI VisionSource::AudioReceive(HAUDIO hAudio, PAUDIODATA pAudioData, ULONG_PTR pUserData)
{
	return;
}

const char* VisionSource::getName()
{
	return "Datapath Capture Card";
}

unsigned int VisionSource::getWidth()
{
	std::scoped_lock<std::mutex> lock(mutex);
	return internalWidth;
}

unsigned int VisionSource::getHeight()
{
	std::scoped_lock<std::mutex> lock(mutex);
	return internalHeight;
}

void VisionSource::activate()
{
}

void VisionSource::deactivate()
{
}

std::string VisionSource::getModeText(unsigned long input)
{
	unsigned long signalWidth = 0;
	unsigned long signalHeight = 0;
	unsigned long signalHz = 0;
	SIGNALTYPE signalType;

	const int modeTextLength = 100;
	char modeText[modeTextLength];

	RGBGetInputSignalType(input, &signalType, &signalWidth, &signalHeight, &signalHz);

	if (RGB_SIGNALTYPE_NOSIGNAL != signalType)
	{
		const int signalTextLength = 16;
		char signalTypeText[signalTextLength];

		switch (signalType)
		{
		case RGB_SIGNALTYPE_COMPOSITE:
		{
			strncpy_s(signalTypeText, "Composite", signalTextLength);
			break;
		}
		case RGB_SIGNALTYPE_DLDVI:
		{
			strncpy_s(signalTypeText, "DVI-DL", signalTextLength);
			break;
		}
		case RGB_SIGNALTYPE_DVI:
		{
			strncpy_s(signalTypeText, "DVI", signalTextLength);
			break;
		}
		case RGB_SIGNALTYPE_SDI:
		{
			strncpy_s(signalTypeText, "SDI", signalTextLength);
			break;
		}
		case RGB_SIGNALTYPE_SVIDEO:
		{
			strncpy_s(signalTypeText, "S-Video", signalTextLength);
			break;
		}
		case RGB_SIGNALTYPE_VGA:
		{
			strncpy_s(signalTypeText, "VGA", signalTextLength);
			break;
		}
		case RGB_SIGNALTYPE_YPRPB:
		{
			strncpy_s(signalTypeText, "YPbPr", signalTextLength);
		}
		default:
			break;
		}

		snprintf(
			modeText,
			modeTextLength,
			TEXT("%s %lux%lu %.5gHz"),
			signalTypeText,
			signalWidth,
			signalHeight,
			static_cast<float>(signalHz) / 1000.0f);
	}
	else
	{
		switch (signalType)
		{
		case RGB_SIGNALTYPE_NOSIGNAL:
		{
			strncpy_s(modeText, "No signal detected", modeTextLength);
			break;
		}
		case RGB_SIGNALTYPE_OUTOFRANGE:
			strncpy_s(modeText, "Signal out of range", modeTextLength);
		default:
			break;
		}
	}

	return std::string(modeText);
}

void VisionSource::update(obs_data_t* settings)
{
	inputId = obs_data_get_int(settings, PROP_INPUT_ID);
	internalWidth = obs_data_get_int(settings, PROP_INTERNAL_WIDTH);
	internalHeight = obs_data_get_int(settings, PROP_INTERNAL_HEIGHT);
}

void VisionSource::getDefaults(obs_data* s)
{
	obs_data_set_default_int(s, PROP_INPUT_ID, 0);
	obs_data_set_default_int(s, PROP_INTERNAL_WIDTH,  640);
	obs_data_set_default_int(s, PROP_INTERNAL_HEIGHT, 480);
}

bool VisionSource::inputSettingModified(obs_properties_t* prop, obs_property_t* p, obs_data_t* settings)
{
	const unsigned long inputId = unsigned long(obs_data_get_int(settings, PROP_INPUT_ID));
	const auto internalWidthProperty  = obs_properties_get(prop, PROP_INTERNAL_WIDTH);
	const auto internalHeightProperty = obs_properties_get(prop, PROP_INTERNAL_HEIGHT);

	HRGB hrgb;
	const auto err = RGBOpenInput(inputId, &hrgb);
	if (err != RGBERROR_NO_ERROR)
	{
		obs_property_set_enabled(internalWidthProperty, false);
		obs_property_set_enabled(internalHeightProperty, false);
		return false;
	}

	unsigned long widthMin, widthMax;
	RGBGetCaptureWidthMinimum(hrgb, &widthMin);
	RGBGetCaptureWidthMaximum(hrgb, &widthMax);

	obs_property_set_enabled(internalWidthProperty, true);
	obs_property_int_set_limits(internalWidthProperty, widthMin, widthMax, 1);

	unsigned long heightMin, heightMax;
	RGBGetCaptureHeightMinimum(hrgb, &heightMin);
	RGBGetCaptureHeightMaximum(hrgb, &heightMax);

	obs_property_set_enabled(internalHeightProperty, true);
	obs_property_int_set_limits(internalHeightProperty, heightMin, heightMax, 1);
	
	RGBCloseInput(hrgb);

	return true;
}

obs_properties* VisionSource::getProperties()
{
	const auto props = obs_properties_create();

	const auto inputPropertyList = obs_properties_add_list(props, PROP_INPUT_ID, "Input", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	{
		unsigned long inputCount;
		RGBGetNumberOfInputs(&inputCount);

		for (unsigned long i = 0; i < inputCount; i++)
		{
			auto modeText = "(" + std::to_string(i) + ") " + getModeText(i);

			obs_property_list_add_int(inputPropertyList, modeText.c_str(), i);
		}

		obs_property_set_modified_callback(inputPropertyList, inputSettingModified);
	}

	// The min/max values should depend on the input
	const auto internalWidthProperty = 
		obs_properties_add_int_slider(props, PROP_INTERNAL_WIDTH, "Internal Width", 1, 0, 1);
	obs_property_set_enabled(internalWidthProperty, false);

	const auto internalHeightProperty =
		obs_properties_add_int_slider(props, PROP_INTERNAL_HEIGHT, "Internal Height", 1, 0, 1);
	obs_property_set_enabled(internalHeightProperty, false);

	return props;
}

obs_source_info VisionSource::getSourceInfo()
{
	obs_source_info info = {};

	info.id = "datapath_vision_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
	info.get_name = [](void* data)
	{
		return reinterpret_cast<VisionSource*>(data)->getName();
	};

	info.create = [](obs_data_t *settings, obs_source_t *source)
	{
		return static_cast<void*>(new VisionSource(source, settings));
	};

	info.destroy = [](void *data)
	{
		delete reinterpret_cast<VisionSource*>(data);
	};

	info.update = [](void *data, obs_data_t* settings)
	{
		return reinterpret_cast<VisionSource*>(data)->update(settings);
	};

	info.get_defaults = [](obs_data_t* settings)
	{
		return VisionSource::getDefaults(settings);
	};

	info.get_properties = [](void* data)
	{
		return VisionSource::getProperties();
	};

	info.activate = [](void *data)
	{
		return reinterpret_cast<VisionSource*>(data)->start();
	};

	info.deactivate = [](void *data)
	{
		return reinterpret_cast<VisionSource*>(data)->stop();
	};

	info.get_width = [](void *data)
	{
		return reinterpret_cast<VisionSource*>(data)->getWidth();
	};

	info.get_height = [](void *data)
	{
		return reinterpret_cast<VisionSource*>(data)->getHeight();
	};

	return info;
}
