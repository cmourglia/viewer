#pragma once

#include "core/defines.h"

struct Environment
{
	u32 envMap        = 0;
	u32 irradianceMap = 0;
	u32 radianceMap   = 0;
	u32 iblDFG        = 0;
};

void LoadEnvironment(const char* filename, Environment* env);