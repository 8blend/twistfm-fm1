/* SPDX-License-Identifier: MIT */
/* Catena effetti sul bus stereo: DIST -> PHASER -> TAPE DELAY -> REVERB.
 * Le linee di ritardo sono a 16 bit per contenere la memoria (~180 KB in tutto).
 * Un effetto con MIX = 0 non viene calcolato. */
#ifndef TFM_FX_H
#define TFM_FX_H
#include <stdint.h>

#define TFM_DIST_SHAPES 10
#define TFM_REV_TYPES 3

/* linea di ritardo con lunghezza potenza di 2 */
typedef struct { int16_t *buf; uint32_t mask, w; } tfm_line;

typedef struct {
    /* distorsione */
    float dist_lp[2], dist_pre[2], dist_fb[2], crush_hold[2];
    uint32_t crush_count;
    /* phaser */
    float ph_z[2][6], ph_last[2], ph_phase, ph_a[2];
    /* tape */
    tfm_line tape;
    float tape_delay, tape_lp, tape_hp, wow_phase, flutter_phase, drift, drift_target;
    uint32_t rnd;
    /* reverb (topologia Dattorro) */
    tfm_line pre, in_ap[4], mod_ap[2], dl1[2], ap2[2], dl2[2], shim;
    float bw_z, damp_z[2], tank_fb[2], lfo, shim_pos;
    /* memoria */
    int16_t m_tape[32768];
    int16_t m_pre[8192];
    int16_t m_in[256 + 256 + 1024 + 512];
    int16_t m_mod[2][2048];
    int16_t m_dl1[2][8192];
    int16_t m_ap2[2][4096];
    int16_t m_dl2[2][8192];
    int16_t m_shim[4096];
} tfm_fx;

void tfm_fx_init(tfm_fx *fx);
/* p = parametri del preset; io = buffer stereo float interleaved (+-1 fondo scala) */
void tfm_fx_process(tfm_fx *fx, const uint8_t *p, float *io, unsigned frames);
void tfm_fx_clear(tfm_fx *fx);
const char *tfm_dist_name(unsigned shape);
float tfm_fx_dist_curve(unsigned shape, float x, float drive01);
#endif
