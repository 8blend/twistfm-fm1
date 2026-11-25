/* SPDX-License-Identifier: MIT */
/* Effetti: distorsione a 10 forme (ispirata ai "character" di Berzerk),
 * phaser a 6 stadi stereo, eco a nastro con wow/flutter/saturazione,
 * riverbero a piastra di Dattorro in tre versioni: ROOM, PLATE, SHIMMER. */
#include "tfm_fx.h"
#include "tfm_engine.h"
#include "tfm_dsp.h"
#include "tfm_util.h"

#define FS ((float)TFM_SAMPLE_RATE)

static const char *const dist_names[TFM_DIST_SHAPES] = {
    "TUBE", "FUZZ", "DIOD", "FOLD", "RECT", "CRSH", "HARD", "SINE", "SPLT", "FDBK"
};
const char *tfm_dist_name(unsigned s) { return s < TFM_DIST_SHAPES ? dist_names[s] : "?"; }

/* ---------- utilita' ---------- */
static inline float u7(const uint8_t *p, unsigned id) { return (float)p[id] * (1.f / 127.f); }

/* sin(2*pi*t), t in cicli (Bhaskara, errore < 0.2%) */
static inline float sinc1(float t)
{
    t -= (float)(int)t;
    if (t < 0) t += 1.f;
    float x = t < 0.5f ? t : t - 0.5f;
    float q = x * (1.f - 2.f * x);
    float y = 32.f * q / (5.f - 8.f * q);
    return t < 0.5f ? y : -y;
}

static inline void line_init(tfm_line *l, int16_t *mem, uint32_t size) { l->buf = mem; l->mask = size - 1; l->w = 0; }
static inline float rd(const tfm_line *l, uint32_t d) { return (float)l->buf[(l->w - d) & l->mask] * (1.f / 32768.f); }
static inline float rdf(const tfm_line *l, float d)
{
    uint32_t i = (uint32_t)d;
    float f = d - (float)i;
    float a = (float)l->buf[(l->w - i) & l->mask], b = (float)l->buf[(l->w - i - 1) & l->mask];
    return (a + (b - a) * f) * (1.f / 32768.f);
}
static inline void wr(tfm_line *l, float x)
{
    float s = x * 32768.f;
    if (s > 32767.f) s = 32767.f;
    if (s < -32768.f) s = -32768.f;
    l->buf[l->w] = (int16_t)s;
    l->w = (l->w + 1) & l->mask;
}
static inline float allpass(tfm_line *l, float x, uint32_t d, float g)
{
    float dd = rd(l, d), v = x - g * dd;
    wr(l, v);
    return dd + g * v;
}
static inline float allpass_f(tfm_line *l, float x, float d, float g)
{
    float dd = rdf(l, d), v = x - g * dd;
    wr(l, v);
    return dd + g * v;
}

void tfm_fx_clear(tfm_fx *fx)
{
    tfm_memset(fx->m_tape, 0, sizeof fx->m_tape);
    tfm_memset(fx->m_pre, 0, sizeof fx->m_pre);
    tfm_memset(fx->m_in, 0, sizeof fx->m_in);
    tfm_memset(fx->m_mod, 0, sizeof fx->m_mod);
    tfm_memset(fx->m_dl1, 0, sizeof fx->m_dl1);
    tfm_memset(fx->m_ap2, 0, sizeof fx->m_ap2);
    tfm_memset(fx->m_dl2, 0, sizeof fx->m_dl2);
    tfm_memset(fx->m_shim, 0, sizeof fx->m_shim);
    fx->tank_fb[0] = fx->tank_fb[1] = 0.f;
    fx->damp_z[0] = fx->damp_z[1] = 0.f;
}

void tfm_fx_init(tfm_fx *fx)
{
    tfm_memset(fx, 0, sizeof *fx);
    line_init(&fx->tape, fx->m_tape, 32768);
    line_init(&fx->pre, fx->m_pre, 8192);
    line_init(&fx->in_ap[0], fx->m_in, 256);
    line_init(&fx->in_ap[1], fx->m_in + 256, 256);
    line_init(&fx->in_ap[2], fx->m_in + 512, 1024);
    line_init(&fx->in_ap[3], fx->m_in + 1536, 512);
    for (int c = 0; c < 2; c++) {
        line_init(&fx->mod_ap[c], fx->m_mod[c], 2048);
        line_init(&fx->dl1[c], fx->m_dl1[c], 8192);
        line_init(&fx->ap2[c], fx->m_ap2[c], 4096);
        line_init(&fx->dl2[c], fx->m_dl2[c], 8192);
    }
    line_init(&fx->shim, fx->m_shim, 4096);
    fx->tape_delay = 0.f;
    fx->rnd = 22222u;
}

/* ---------- distorsione ---------- */
static inline float shape(tfm_fx *fx, unsigned s, float x, int ch, float drive01)
{
    switch (s) {
    default:
    case 0: return tfm_tanhf(x + 0.25f) - 0.2449f;                                /* TUBE: asimmetrica calda */
    case 1: return tfm_clampf(tfm_tanhf(3.f * x) * 1.25f, -1.f, 1.f) * 0.45f;      /* FUZZ: quasi quadra */
    case 2: return x > 0 ? tfm_tanhf(x) : tfm_tanhf(2.5f * x) * 0.6f;              /* DIODE */
    case 3: { float t = x * 0.25f + 0.25f; t -= (float)(int)t; if (t < 0) t += 1.f; /* FOLD: ripiegamento */
              float y = 4.f * (t < 0.5f ? t : 1.f - t) - 1.f; return y; }
    case 4: { float y = tfm_tanhf(x); return 0.5f * y + 0.8f * (y < 0 ? -y : y) - 0.4f; } /* RECT: ottava sopra */
    case 5: {                                                                      /* CRUSH: bit e campioni */
        unsigned hold = 1u + (unsigned)(drive01 * 15.f);
        if (ch == 0) fx->crush_count++;
        if (fx->crush_count % hold == 0 || hold == 1) {
            float levels = tfm_exp2f(12.f - drive01 * 9.f);
            float y = tfm_tanhf(x) * levels;
            y = (float)(int)(y + (y < 0 ? -0.5f : 0.5f)) / levels;
            fx->crush_hold[ch] = y;
        }
        return fx->crush_hold[ch];
    }
    case 6: return tfm_clampf(x, -1.f, 1.f);                                       /* HARD */
    case 7: return sinc1(x * 0.25f);                                               /* SINE: piega sinusoidale */
    case 8: return x > 0 ? tfm_tanhf(0.7f * x) : tfm_clampf(x, -0.4f, 0.f) * 2.f;  /* SPLIT */
    case 9: { float y = tfm_tanhf(x + 1.7f * fx->dist_fb[ch]); fx->dist_fb[ch] = y; return y * 0.6f; } /* FEEDBACK */
    }
}

static void dist_run(tfm_fx *fx, const uint8_t *p, float *io, unsigned n)
{
    unsigned s = p[TFM_P_DIST_TYPE] < TFM_DIST_SHAPES ? p[TFM_P_DIST_TYPE] : 0;
    float d01 = u7(p, TFM_P_DIST_DRIVE), mix = u7(p, TFM_P_DIST_MIX);
    float gain = 1.5f * tfm_exp2f(d01 * 5.5f);
    float temp = u7(p, TFM_P_DIST_TEMP);
    /* TEMPERATURE: freddo = pre-enfasi degli acuti, caldo = passa-basso dopo la distorsione */
    float fc = 18000.f * tfm_exp2f(-temp * 3.9f);                  /* 18 kHz .. 1.2 kHz */
    float c = 1.f - tfm_exp2f(-6.2832f * fc / FS * 1.4427f);
    float pre = (1.f - temp) * 1.2f;
    float out_gain = 0.55f;
    for (unsigned i = 0; i < n; i++)
        for (int ch = 0; ch < 2; ch++) {
            float x = io[i * 2 + ch];
            float hp = x - fx->dist_pre[ch];
            fx->dist_pre[ch] += 0.2f * hp;
            float y = shape(fx, s, (x + pre * hp) * gain, ch, d01);
            fx->dist_lp[ch] += c * (y - fx->dist_lp[ch]);
            y = fx->dist_lp[ch] * out_gain;
            io[i * 2 + ch] = x + (y - x) * mix;
        }
}

/* ---------- phaser ---------- */
static void phaser_run(tfm_fx *fx, const uint8_t *p, float *io, unsigned n)
{
    float rate = 0.03f * tfm_exp2f(u7(p, TFM_P_PH_RATE) * 8.f);    /* 0.03 .. 7.7 Hz */
    float depth = u7(p, TFM_P_PH_DEPTH);
    float fb = u7(p, TFM_P_PH_FB) * 0.85f;
    float mix = u7(p, TFM_P_PH_MIX);
    for (unsigned i = 0; i < n; i++) {
        if ((i & 15) == 0) { /* coefficienti aggiornati ogni 16 campioni */
            fx->ph_phase += rate * 16.f / FS;
            if (fx->ph_phase >= 1.f) fx->ph_phase -= 1.f;
            for (int ch = 0; ch < 2; ch++) {
                float l = 0.5f + 0.5f * sinc1(fx->ph_phase + (ch ? 0.25f : 0.f));
                float f = 180.f * tfm_exp2f(depth * 4.4f * l);       /* 180 Hz .. ~3.8 kHz */
                float t = tfm_prewarp(f / FS);
                fx->ph_a[ch] = (1.f - t) / (1.f + t);
            }
        }
        for (int ch = 0; ch < 2; ch++) {
            float x = io[i * 2 + ch], a = fx->ph_a[ch];
            float y = x + fb * fx->ph_last[ch];
            for (int k = 0; k < 6; k++) {
                float o = a * y + fx->ph_z[ch][k];
                fx->ph_z[ch][k] = y - a * o;
                y = o;
            }
            fx->ph_last[ch] = y;
            io[i * 2 + ch] = x * (1.f - 0.5f * mix) + y * 0.5f * mix;
        }
    }
}

/* ---------- eco a nastro ---------- */
static void tape_run(tfm_fx *fx, const uint8_t *p, float *io, unsigned n)
{
    float ms = 30.f * tfm_exp2f(u7(p, TFM_P_DLY_TIME) * 4.544f);   /* 30 .. 700 ms */
    if (p[TFM_P_DLY_SYNC]) {   /* a tempo: divisione della nota da 1/4 al BPM del preset (o del clock esterno) */
        float bpm = 60.f + (float)p[TFM_P_BPM];
        ms = 60000.f / bpm * tfm_dly_div_beats[p[TFM_P_DLY_DIV] % TFM_DLY_DIVS];
        if (ms > 720.f) ms = 720.f;   /* il nastro e' lungo ~740 ms */
    }
    float target = ms * (FS / 1000.f);
    float fb = u7(p, TFM_P_DLY_FB) * 1.06f;                        /* nell'ultimo tratto: autooscillazione */
    float tone = u7(p, TFM_P_DLY_TONE);
    float fc = 900.f * tfm_exp2f(tone * 4.f);                      /* 0.9 .. 14 kHz */
    float c = 1.f - tfm_exp2f(-6.2832f * fc / FS * 1.4427f);
    float wow = u7(p, TFM_P_DLY_WOW), sat = u7(p, TFM_P_DLY_SAT);
    float spread = u7(p, TFM_P_DLY_SPREAD) * 0.025f * FS;
    float mix = u7(p, TFM_P_DLY_MIX);
    float drive = 1.f + sat * 4.f, norm = 1.f / drive;   /* guadagno 1 a basso livello: la saturazione comprime soltanto */
    if (fx->tape_delay <= 0.f) fx->tape_delay = target;
    for (unsigned i = 0; i < n; i++) {
        /* la velocita' del nastro insegue il tempo impostato: cambiando TIME si sente il "pitch" */
        fx->tape_delay += (target - fx->tape_delay) * 0.0004f;
        fx->wow_phase += 0.55f / FS; if (fx->wow_phase >= 1.f) fx->wow_phase -= 1.f;
        fx->flutter_phase += 6.3f / FS; if (fx->flutter_phase >= 1.f) fx->flutter_phase -= 1.f;
        if ((i & 2047) == 0) { fx->rnd = fx->rnd * 1664525u + 1013904223u; fx->drift_target = (float)(int32_t)fx->rnd * (1.f / 2147483648.f); }
        fx->drift += (fx->drift_target - fx->drift) * 0.0002f;
        float d = fx->tape_delay + wow * (22.f * sinc1(fx->wow_phase) + 3.f * sinc1(fx->flutter_phase) + 8.f * fx->drift);
        if (d < 2.f) d = 2.f;
        if (d > 32000.f - spread) d = 32000.f - spread;
        float yl = rdf(&fx->tape, d);
        float yr = spread > 1.f ? rdf(&fx->tape, d + spread) : yl;
        /* ritorno: passa-basso (testina consumata), passa-alto, saturazione del nastro */
        fx->tape_lp += c * (yl - fx->tape_lp);
        fx->tape_hp += 0.0115f * (fx->tape_lp - fx->tape_hp);
        float f = tfm_tanhf((fx->tape_lp - fx->tape_hp) * drive) * norm;
        float in = (io[i * 2] + io[i * 2 + 1]) * 0.5f;
        wr(&fx->tape, tfm_tanhf((in + fb * f) * drive) * norm);
        io[i * 2] += yl * mix;
        io[i * 2 + 1] += yr * mix;
    }
}

/* ---------- riverbero ---------- */
/* lunghezze originali di Dattorro (a 29761 Hz) */
static const float K = 44100.f / 29761.f;

static void reverb_run(tfm_fx *fx, const uint8_t *p, float *io, unsigned n)
{
    unsigned type = p[TFM_P_REV_TYPE] < TFM_REV_TYPES ? p[TFM_P_REV_TYPE] : 0;
    static const float size_t3[3] = { 0.42f, 0.85f, 1.f };
    static const float dmin[3] = { 0.45f, 0.50f, 0.70f }, dmax[3] = { 0.88f, 0.95f, 0.975f };
    float sz = size_t3[type] * K;
    float dec = dmin[type] + (dmax[type] - dmin[type]) * u7(p, TFM_P_REV_DECAY);
    float damp = 0.75f - 0.73f * u7(p, TFM_P_REV_TONE);
    float mix = u7(p, TFM_P_REV_MIX);
    uint32_t pd = 1u + (uint32_t)(u7(p, TFM_P_REV_PREDLY) * 0.18f * FS);    /* fino a 180 ms */
    float exc = 2.f + u7(p, TFM_P_REV_MOD) * 22.f;
    /* il rientro dello shimmer si riduce quando la coda e' gia' lunga: resta stabile */
    float shim = type == 2 ? u7(p, TFM_P_REV_SHIM) * 0.9f * (1.f - dec) : 0.f;
    /* lunghezze scalate */
    uint32_t i0 = (uint32_t)(142 * K), i1 = (uint32_t)(107 * K), i2 = (uint32_t)(379 * K), i3 = (uint32_t)(277 * K);
    float m0 = 672.f * sz, m1 = 908.f * sz;
    uint32_t d10 = (uint32_t)(4453 * sz), d11 = (uint32_t)(4217 * sz);
    uint32_t a20 = (uint32_t)(1800 * sz), a21 = (uint32_t)(2656 * sz);
    uint32_t d20 = (uint32_t)(3720 * sz), d21 = (uint32_t)(3163 * sz);
#define T(x) ((uint32_t)((x) * sz) + 1u)
    for (unsigned i = 0; i < n; i++) {
        float x = (io[i * 2] + io[i * 2 + 1]) * 0.5f;
        wr(&fx->pre, x);
        x = rd(&fx->pre, pd);
        x = allpass(&fx->in_ap[0], x, i0, 0.75f);
        x = allpass(&fx->in_ap[1], x, i1, 0.75f);
        x = allpass(&fx->in_ap[2], x, i2, 0.625f);
        x = allpass(&fx->in_ap[3], x, i3, 0.625f);
        /* shimmer: un'ottava sopra del suono della coda, rimandata nella vasca */
        float sh = 0.f;
        if (shim > 0.f) {
            const float W = 2048.f;
            fx->shim_pos += 1.f;
            if (fx->shim_pos >= W) fx->shim_pos -= W;
            float f1 = fx->shim_pos / W, f2 = f1 + 0.5f;
            if (f2 >= 1.f) f2 -= 1.f;
            float g1 = 1.f - (f1 < 0.5f ? 1.f - 2.f * f1 : 2.f * f1 - 1.f);
            float g2 = 1.f - (f2 < 0.5f ? 1.f - 2.f * f2 : 2.f * f2 - 1.f);
            sh = (rdf(&fx->shim, (1.f - f1) * W + 1.f) * g1 + rdf(&fx->shim, (1.f - f2) * W + 1.f) * g2) * shim;
        }
        fx->lfo += 0.9f / FS;
        if (fx->lfo >= 1.f) fx->lfo -= 1.f;
        /* ramo sinistro */
        float tl = x + dec * fx->tank_fb[1] + sh;
        tl = allpass_f(&fx->mod_ap[0], tl, m0 + exc * (1.f + sinc1(fx->lfo)), -0.7f);
        wr(&fx->dl1[0], tl);
        tl = rd(&fx->dl1[0], d10);
        fx->damp_z[0] = tl + (fx->damp_z[0] - tl) * damp;
        tl = allpass(&fx->ap2[0], fx->damp_z[0] * dec, a20, 0.5f);
        wr(&fx->dl2[0], tl);
        fx->tank_fb[0] = rd(&fx->dl2[0], d20);
        /* ramo destro */
        float tr = x + dec * fx->tank_fb[0] + sh;
        tr = allpass_f(&fx->mod_ap[1], tr, m1 + exc * (1.f + sinc1(fx->lfo + 0.25f)), -0.7f);
        wr(&fx->dl1[1], tr);
        tr = rd(&fx->dl1[1], d11);
        fx->damp_z[1] = tr + (fx->damp_z[1] - tr) * damp;
        tr = allpass(&fx->ap2[1], fx->damp_z[1] * dec, a21, 0.5f);
        wr(&fx->dl2[1], tr);
        fx->tank_fb[1] = rd(&fx->dl2[1], d21);
        /* uscite: prese multiple dalla vasca */
        float yl = rd(&fx->dl1[1], T(266)) + rd(&fx->dl1[1], T(2974)) - rd(&fx->ap2[1], T(1913)) + rd(&fx->dl2[1], T(1996))
                 - rd(&fx->dl1[0], T(1990)) - rd(&fx->ap2[0], T(187)) - rd(&fx->dl2[0], T(1066));
        float yr = rd(&fx->dl1[0], T(353)) + rd(&fx->dl1[0], T(3627)) - rd(&fx->ap2[0], T(1228)) + rd(&fx->dl2[0], T(2673))
                 - rd(&fx->dl1[1], T(2111)) - rd(&fx->ap2[1], T(335)) - rd(&fx->dl2[1], T(121));
        if (shim > 0.f) wr(&fx->shim, tfm_tanhf((yl + yr) * 0.3f));
        io[i * 2] += yl * 0.6f * mix;
        io[i * 2 + 1] += yr * 0.6f * mix;
    }
#undef T
}

/* Curva statica della distorsione, solo per il disegno sullo schermo. */
float tfm_fx_dist_curve(unsigned s, float x, float drive01)
{
    float g = 1.5f * tfm_exp2f(drive01 * 5.5f) * 0.25f, v = x * g;
    switch (s) {
    case 5: { float levels = tfm_exp2f(12.f - drive01 * 9.f); float y = tfm_tanhf(v) * levels;
              return (float)(int)(y + (y < 0 ? -0.5f : 0.5f)) / levels; }
    case 9: return tfm_tanhf(v * 2.f) * 0.6f;
    default: break;
    }
    /* per le forme senza stato si usa la stessa funzione del suono */
    float y;
    switch (s) {
    case 0: y = tfm_tanhf(v + 0.25f) - 0.2449f; break;
    case 1: y = tfm_clampf(tfm_tanhf(3.f * v) * 1.25f, -1.f, 1.f) * 0.45f / 0.45f; break;
    case 2: y = v > 0 ? tfm_tanhf(v) : tfm_tanhf(2.5f * v) * 0.6f; break;
    case 3: { float t = v * 0.25f + 0.25f; t -= (float)(int)t; if (t < 0) t += 1.f; y = 4.f * (t < 0.5f ? t : 1.f - t) - 1.f; break; }
    case 4: { float q = tfm_tanhf(v); y = 0.5f * q + 0.8f * (q < 0 ? -q : q) - 0.4f; break; }
    case 6: y = tfm_clampf(v, -1.f, 1.f); break;
    case 7: y = sinc1(v * 0.25f); break;
    case 8: y = v > 0 ? tfm_tanhf(0.7f * v) : tfm_clampf(v, -0.4f, 0.f) * 2.f; break;
    default: y = v; break;
    }
    return y;
}

void tfm_fx_process(tfm_fx *fx, const uint8_t *p, float *io, unsigned frames)
{
    uint8_t slot[4] = { p[TFM_P_FX_SLOT1], p[TFM_P_FX_SLOT2], p[TFM_P_FX_SLOT3], p[TFM_P_FX_SLOT4] };
    unsigned done = 0, tape_used = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned f = slot[i];
        if (f < 1 || f > 4 || (done & (1u << f))) continue; /* ogni effetto al massimo una volta */
        done |= 1u << f;
        switch (f) {
        case 1: if (p[TFM_P_DIST_MIX]) dist_run(fx, p, io, frames); break;
        case 2: if (p[TFM_P_PH_MIX]) phaser_run(fx, p, io, frames); break;
        case 3: if (p[TFM_P_DLY_MIX]) { tape_run(fx, p, io, frames); tape_used = 1; } break;
        case 4: if (p[TFM_P_REV_MIX]) reverb_run(fx, p, io, frames); break;
        }
    }
    if (!tape_used) fx->tape_delay = 0.f;
}
