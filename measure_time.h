//
// Copyright © 2021-2022, David Priver
//
#ifndef MEASURE_TIME_H
#define MEASURE_TIME_H
#include <stdint.h>

//
// Gets current monotonically increasing time, measured in microseconds.
// Always succeeds.
// Used for ad-hoc profiling of different parts of the program.
//
static inline uint64_t get_t(void);

#if defined(__linux__) || defined(__APPLE__)

#include <time.h>
#include <stdint.h>
#include <unistd.h>
// returns microseconds
static inline
uint64_t
get_t(void){
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return t.tv_sec * 1000000llu + t.tv_nsec/1000;
}

#elif defined(_WIN32)

#include <assert.h>
#include "Platform/Windows/windowsheader.h"
// Due to this static, we should call this very soon after entering main.
static LARGE_INTEGER freq;

// returns microseconds
static inline
uint64_t
get_t(void){
    LARGE_INTEGER time;
    if(!freq.QuadPart){
        // This should never fail.
        // "On systems that run Windows XP or later, the function will always
        // succeed and will thus never return zero."
        BOOL ok = QueryPerformanceFrequency(&freq);
        assert(ok == TRUE);
    }

    // This should never fail.
    // "On systems that run Windows XP or later, the function will always
    // succeed and will thus never return zero."
    BOOL ok = QueryPerformanceCounter(&time);
    assert(ok == TRUE);
    return  (1000000llu * time.QuadPart) / freq.QuadPart;
}
#elif defined(WASM)

static inline
uint64_t
get_t(void){
    return 0;
}
#endif

#endif
