// MinGW-only shim for the VST3 SDK's data-exchange utility, which calls std::aligned_alloc.
// The MSVCRT headers don't declare it. Naginata never uses data exchange, so the allocation path
// below never runs. The shim exists only so the SDK's sdk target compiles.
#pragma once
#if defined(__MINGW32__)
#include <cstddef>
#include <cstdlib>
#include <malloc.h>
namespace std {
inline void* aligned_alloc(size_t alignment, size_t size) { return _aligned_malloc(size, alignment); }
}  // namespace std
#endif
