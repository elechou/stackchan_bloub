/*
 * Tiny float math, no libm.
 *
 * The firmware is bare metal and links nothing but libgcc, so the handful of
 * functions the eye model needs live here. Everything is single precision:
 * the ESP32-S3 FPU does add/mul natively and has no double support.
 */
#ifndef FMATH_H
#define FMATH_H

#include <stdint.h>

static inline float f_abs(float x) { return x < 0.0f ? -x : x; }
static inline float f_min(float a, float b) { return a < b ? a : b; }
static inline float f_max(float a, float b) { return a > b ? a : b; }
static inline float f_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float f_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* 1/sqrt(x) for x > 0. Two Newton steps: relative error below 5e-6. */
static inline float f_rsqrt(float x)
{
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    float h = 0.5f * x;
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    return y;
}

static inline float f_sqrt(float x) { return x > 1e-20f ? x * f_rsqrt(x) : 0.0f; }

/* sin(2*pi*turns). Angles are carried in turns so range reduction is exact. */
float f_sin_turns(float turns);
static inline float f_cos_turns(float turns) { return f_sin_turns(turns + 0.25f); }

#define F_DEG_TO_TURNS (1.0f / 360.0f)
#define F_RAD_TO_TURNS 0.15915494309f

#endif
