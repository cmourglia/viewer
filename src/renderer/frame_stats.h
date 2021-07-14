#pragma once

#include "core/defines.h"

struct FrameStats
{
	static FrameStats* Get();

	struct
	{
		f64 loadTexture   = 0.0;
		f64 precomputeDFG = 0.0;
		f64 cubemap       = 0.0;
		f64 prefilter     = 0.0;
		f64 irradiance    = 0.0;
		f64 total         = 0.0;
	} ibl;

	f64 loadScene = 0.0;

	struct
	{
		f64 updatePrograms       = 0.0;
		f64 zPrepass             = 0.0;
		f64 renderModels         = 0.0;
		f64 background           = 0.0;
		f64 resolveMSAA          = 0.0;
		f64 highpassAndLuminance = 0.0;
		f64 bloomDownsample      = 0.0;
		f64 bloomUpsample        = 0.0;
		f64 bloomTotal           = 0.0;
		f64 finalCompositing     = 0.0;
	} frame;

	f64 renderTotal = 0.0;
	f64 frameTotal  = 0.0;

	f64 imguiDesc   = 0.0;
	f64 imguiRender = 0.0;
};
