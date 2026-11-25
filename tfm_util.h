/* SPDX-License-Identifier: MIT */
/* Piccole funzioni di memoria e stringhe: il firmware FM-1 non ha string.h completo. */
#ifndef TFM_UTIL_H
#define TFM_UTIL_H
#include <stdint.h>
static inline void tfm_memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; }
static inline void tfm_memcpy(void *d, const void *s, unsigned n)
{ uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; }
static inline unsigned tfm_strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static inline void tfm_strcpy(char *d, const char *s) { while ((*d++ = *s++) != 0) {} }
#endif
