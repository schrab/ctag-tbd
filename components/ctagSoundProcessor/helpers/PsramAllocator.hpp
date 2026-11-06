#pragma once

// PSRAM-backed STL allocator (ported from servandobarreiro/ctag-tbd b69cb179,
// doc/servando_tbd16_optimizations.md §2.1): routes std::map red-black tree
// nodes and std::function targets to Octal PSRAM. The parameter maps
// (pMapPar/pMapCv/pMapTrig, hundreds of ~64-80 B nodes on complex plugins)
// are only touched on parameter edits and preset loads — never from
// Process() — so PSRAM residency costs nothing audible and frees ~5-25 KB
// of internal SRAM (2026-11-07, user-approved servando port).

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

#if defined(TBD_SIM)
// Host simulator: no esp_heap_caps, use the default heap.
namespace CTAG { namespace SP { namespace HELPERS {

template <typename T>
struct PsramAllocator {
    using value_type = T;
    PsramAllocator() noexcept = default;
    template <typename U> PsramAllocator(const PsramAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        return static_cast<T*>(std::malloc(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept {
        std::free(p);
    }
};

}}} // namespace

#else
#include "esp_heap_caps.h"

namespace CTAG { namespace SP { namespace HELPERS {

template <typename T>
struct PsramAllocator {
    using value_type = T;
    PsramAllocator() noexcept = default;
    template <typename U> PsramAllocator(const PsramAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
        if (p == nullptr) {
            // PSRAM exhausted (e.g. the sample ROM buffered most of it):
            // fall back to the default heap rather than fail the load.
            p = std::malloc(n * sizeof(T));
        }
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept {
        // heap_caps_free releases both PSRAM and default-heap blocks.
        heap_caps_free(p);
    }
};

}}} // namespace

#endif // TBD_SIM

namespace CTAG { namespace SP { namespace HELPERS {

template <class T, class U>
inline bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return true; }
template <class T, class U>
inline bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return false; }

template <typename T>
using PsramVector = std::vector<T, PsramAllocator<T>>;

// PSRAM-backed std::map<std::string, V>.
template <typename V>
using PsramStringMap = std::map<
    std::string, V,
    std::less<std::string>,
    PsramAllocator<std::pair<const std::string, V>>
>;

}}} // namespace CTAG::SP::HELPERS
