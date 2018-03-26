#include <obs-module.h>

#include "VisionSource.hpp"

#include <RGBAPI.H>
#include <RGBERROR.H>

static HRGBDLL hRGBDLL = 0;

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
	auto sourceInfo = VisionSource::getSourceInfo();
	obs_register_source(&sourceInfo);

	if (RGBLoad(&hRGBDLL) != RGBERROR_NO_ERROR)
	{
		blog(LOG_ERROR, "Could not initialize rgbeasy");
		return false;
	}

	return true;
}

void obs_module_unload(void)
{
	if (!hRGBDLL)
	{
		RGBFree(hRGBDLL);
	}
}
