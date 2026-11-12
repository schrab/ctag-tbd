// Host-test shim for the ESP-IDF heap_caps API. The vendored rapidjson fork
// routes its allocator through heap_caps_*; on the host plain libc is fine.
#pragma once

#include <cstdlib>

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2

inline void* heap_caps_malloc_prefer(size_t size, int n, ...) {
    (void)n;
    return malloc(size);
}

inline void* heap_caps_realloc_prefer(void* p, size_t size, int n, ...) {
    (void)n;
    return realloc(p, size);
}

inline void heap_caps_free(void* p) { free(p); }
