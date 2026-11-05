#pragma once
#include "stub_all.h"
struct CPUInfoStub { bool bAVX = true, bFMA = true; };
inline CPUInfoStub cpu_info;
