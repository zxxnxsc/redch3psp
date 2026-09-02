#pragma once

#include <cstdio>

enum { DBG_CRITICAL = 0 };

#define VMU_DEFAULT_PATH ""
#define RAIIVmuBeep(...)

template<typename... Args>
inline void dbglog(int, const char *format, Args... args)
{
    std::fprintf(stderr, format, args...);
}
