#ifndef WINDOWSHEADER_H
#define WINDOWSHEADER_H
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
// Idk why, but this conflicts with builtin
// so hacky #define
#define _mm_prefetch _WINDOWS_MM_PREFETCH
#include <Windows.h>
#undef _mm_prefetch
#undef ERROR

#endif
