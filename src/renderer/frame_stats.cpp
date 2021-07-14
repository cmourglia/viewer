#include "renderer/frame_stats.h"

FrameStats* FrameStats::Get()
{
	static FrameStats stats = {};
	return &stats;
}