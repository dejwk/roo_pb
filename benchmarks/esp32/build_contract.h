#pragma once

// Fail instead of printing misleading release-build metadata.
#ifndef __OPTIMIZE_SIZE__
#error "ESP32 benchmark requires size optimization (-Os)"
#endif

#ifdef __cplusplus
#if __cplusplus != 201703L
#error "ESP32 benchmark requires C++17"
#endif
#if defined(__EXCEPTIONS) || defined(__GXX_RTTI)
#error "ESP32 benchmark requires exceptions and RTTI disabled"
#endif
#endif
