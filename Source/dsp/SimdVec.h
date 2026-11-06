// SPDX-License-Identifier: AGPL-3.0-or-later
// Resonance Verb — minimal portable 4-wide float SIMD wrapper.
// SSE2 on x86-64, NEON on ARM (Apple Silicon / aarch64 Linux / Windows ARM64),
// scalar fallback elsewhere (or when RV_FORCE_SCALAR is defined, for verification).
#pragma once

#if ! defined(RV_FORCE_SCALAR) && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
  #include <emmintrin.h>
  #define RV_SIMD_SSE 1
#elif ! defined(RV_FORCE_SCALAR) && (defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64))
  #include <arm_neon.h>
  #define RV_SIMD_NEON 1
#else
  #define RV_SIMD_SCALAR 1
#endif

namespace rv
{
struct alignas (16) Vec4
{
#if defined(RV_SIMD_SSE)
    __m128 v;
    static inline Vec4 load (const float* p) noexcept      { return { _mm_load_ps (p) }; }
    inline void store (float* p) const noexcept             { _mm_store_ps (p, v); }
    static inline Vec4 broadcast (float x) noexcept         { return { _mm_set1_ps (x) }; }
    static inline Vec4 zero() noexcept                      { return { _mm_setzero_ps() }; }
    friend inline Vec4 operator+ (Vec4 a, Vec4 b) noexcept  { return { _mm_add_ps (a.v, b.v) }; }
    friend inline Vec4 operator- (Vec4 a, Vec4 b) noexcept  { return { _mm_sub_ps (a.v, b.v) }; }
    friend inline Vec4 operator* (Vec4 a, Vec4 b) noexcept  { return { _mm_mul_ps (a.v, b.v) }; }
#elif defined(RV_SIMD_NEON)
    float32x4_t v;
    static inline Vec4 load (const float* p) noexcept      { return { vld1q_f32 (p) }; }
    inline void store (float* p) const noexcept             { vst1q_f32 (p, v); }
    static inline Vec4 broadcast (float x) noexcept         { return { vdupq_n_f32 (x) }; }
    static inline Vec4 zero() noexcept                      { return { vdupq_n_f32 (0.0f) }; }
    friend inline Vec4 operator+ (Vec4 a, Vec4 b) noexcept  { return { vaddq_f32 (a.v, b.v) }; }
    friend inline Vec4 operator- (Vec4 a, Vec4 b) noexcept  { return { vsubq_f32 (a.v, b.v) }; }
    friend inline Vec4 operator* (Vec4 a, Vec4 b) noexcept  { return { vmulq_f32 (a.v, b.v) }; }
#else
    float v[4];
    static inline Vec4 load (const float* p) noexcept      { return { { p[0], p[1], p[2], p[3] } }; }
    inline void store (float* p) const noexcept             { for (int i = 0; i < 4; ++i) p[i] = v[i]; }
    static inline Vec4 broadcast (float x) noexcept         { return { { x, x, x, x } }; }
    static inline Vec4 zero() noexcept                      { return broadcast (0.0f); }
    friend inline Vec4 operator+ (Vec4 a, Vec4 b) noexcept  { return { { a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3] } }; }
    friend inline Vec4 operator- (Vec4 a, Vec4 b) noexcept  { return { { a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3] } }; }
    friend inline Vec4 operator* (Vec4 a, Vec4 b) noexcept  { return { { a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3] } }; }
#endif

    inline float sum() const noexcept
    {
        alignas (16) float t[4];
        store (t);
        return (t[0] + t[1]) + (t[2] + t[3]);
    }
};
} // namespace rv
