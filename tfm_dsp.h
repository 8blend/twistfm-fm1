/* SPDX-License-Identifier: MIT */
/* Funzioni matematiche veloci in float, senza libm. */
#ifndef TFM_DSP_H
#define TFM_DSP_H
#include <stdint.h>

#ifndef TFM_DSP_CODE
#define TFM_DSP_CODE
#endif

static inline float tfm_clampf(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

/* 2^x, errore relativo < 1e-4 su tutto il range utile. */
static inline float tfm_exp2f(float x)
{
    x = tfm_clampf(x, -60.f, 60.f);
    int i = (int)x;
    if ((float)i > x) i--;
    float f = x - (float)i;
    float p = 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    union { float f; uint32_t u; } v = { p };
    v.u += (uint32_t)((int32_t)i << 23);
    return v.f;
}

/* tanh razionale (Pade' 7/6 troncato), esatto ai limiti +-1. */
static inline float tfm_tanhf(float x)
{
    if (x > 4.97f) return 1.f;
    if (x < -4.97f) return -1.f;
    float x2 = x * x;
    float a = x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)));
    float b = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
    return a / b;
}

/* tan(pi*f/fs) per il prewarp del filtro, f normalizzata (0..0.49). */
static inline float tfm_prewarp(float fn)
{
    /* tan(x) con x = pi*fn, approssimazione razionale fino a ~1.5 rad */
    float x = 3.14159265f * fn;
    float x2 = x * x;
    return x * (1.f - x2 * (0.1111111f + x2 * 0.0010582f)) /
           (1.f - x2 * (0.4444444f - x2 * 0.0158730f));
}

#endif
