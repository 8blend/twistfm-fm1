/* SPDX-License-Identifier: MIT */
#include "tfm_engine.h"
#include "tfm_dsp.h"
#include "tfm_tables.h"
#include "tfm_fx.h"
#include "tfm_util.h"

/* ------------------------------------------------------------------ */
/* Operatore OPL3                                                      */
/* ------------------------------------------------------------------ */

enum { EG_OFF, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };
#define EG_MAX (511u << 16)

typedef struct {
    uint32_t phase, inc;
    uint32_t env;          /* attenuazione Q16, 0 = massimo */
    uint32_t sl;           /* livello di sustain Q16 */
    uint16_t att;          /* TL + KSL + velocity, unita' 0.1875 dB */
    int16_t out, prev;     /* uscita (per modulazione/feedback) */
    uint8_t stage, wave;
    uint8_t ar, dr, rr;    /* rate effettivi 0..63 */
    uint8_t silent;        /* volume 0: non si calcola */
} tfm_op;

/* Uscita di un operatore come nel chip: logsin + attenuazione -> exp. */
static TFM_DSP_CODE inline int16_t op_wave(unsigned phase, unsigned wave, unsigned env)
{
    unsigned level;
    int neg = 0;
    phase &= 1023;
    switch (wave & 7) {
    default:
    case 0: /* sine */
        neg = (phase & 0x200) != 0;
        level = tfm_logsin[(phase & 0x100) ? (~phase & 0xff) : (phase & 0xff)];
        break;
    case 1: /* half-sine */
        if (phase & 0x200) return 0;
        level = tfm_logsin[(phase & 0x100) ? (~phase & 0xff) : (phase & 0xff)];
        break;
    case 2: /* abs-sine */
        level = tfm_logsin[(phase & 0x100) ? (~phase & 0xff) : (phase & 0xff)];
        break;
    case 3: /* pulse-sine */
        if (phase & 0x100) return 0;
        level = tfm_logsin[phase & 0xff];
        break;
    case 4: /* even-sine */
        if (phase & 0x200) return 0;
        neg = (phase & 0x300) == 0x100;
        level = tfm_logsin[(phase & 0x80) ? (((phase ^ 0xff) << 1) & 0xff) : ((phase << 1) & 0xff)];
        break;
    case 5: /* even-abs-sine */
        if (phase & 0x200) return 0;
        level = tfm_logsin[(phase & 0x80) ? (((phase ^ 0xff) << 1) & 0xff) : ((phase << 1) & 0xff)];
        break;
    case 6: /* square */
        neg = (phase & 0x200) != 0;
        level = 0;
        break;
    case 7: /* derived square */
        if (phase & 0x200) { neg = 1; phase = (phase & 0x1ff) ^ 0x1ff; }
        level = phase << 3;
        break;
    }
    level += env << 3;
    if (level > 0x1fff) level = 0x1fff;
    int v = (int)((tfm_exp[level & 0xff] << 1) >> (level >> 8));
    return (int16_t)(neg ? ~v : v);
}

/* Avanza l'inviluppo di un campione. Restituisce l'attenuazione 0..511. */
static TFM_DSP_CODE inline unsigned op_env(tfm_op *o)
{
    switch (o->stage) {
    case EG_ATTACK: {
        /* moltiplicazione in float: niente prodotti a 64 bit (vedi felucca-bsp docs/CPU.md) */
        o->env = (uint32_t)((float)o->env * tfm_eg_attack[o->ar]);
        if (o->env < (1u << 16)) { o->env = 0; o->stage = EG_DECAY; }
        break;
    }
    case EG_DECAY:
        o->env += tfm_eg_decay[o->dr];
        if (o->env >= o->sl) { o->env = o->sl; o->stage = EG_SUSTAIN; }
        break;
    case EG_RELEASE:
        o->env += tfm_eg_decay[o->rr];
        if (o->env >= EG_MAX) { o->env = EG_MAX; o->stage = EG_OFF; }
        break;
    default:
        break;
    }
    unsigned a = (o->env >> 16) + o->att;
    return a > 511 ? 511 : a;
}

static TFM_DSP_CODE inline int16_t op_calc(tfm_op *o, int mod)
{
    if (o->silent) return 0;   /* operatore spento: niente calcolo */
    unsigned env = op_env(o);
    int16_t out = op_wave((o->phase >> 22) + (unsigned)mod, o->wave, env);
    o->phase += o->inc;
    o->prev = o->out;
    o->out = out;
    return out;
}

/* ------------------------------------------------------------------ */
/* Filtro a 4 poli con pole mixing (15 modi stile Xpander)             */
/* ------------------------------------------------------------------ */

typedef struct { float s[4]; float y4; uint32_t noise; } tfm_filter;

/* Coefficienti y0..y4 per ogni modo; l'ultimo valore e' il guadagno. */
static const float flt_mix[TFM_FLT_MODES][6] = {
    { 0, 0, 0, 0, 1, 1.0f },    /* LP4 */
    { 0, 0, 0, 1, 0, 1.0f },    /* LP3 */
    { 0, 0, 1, 0, 0, 1.0f },    /* LP2 */
    { 0, 1, 0, 0, 0, 1.0f },    /* LP1 */
    { 0, 0, 1, -2, 1, 3.0f },   /* BP4 */
    { 0, 1, -1, 0, 0, 2.0f },   /* BP2 */
    { 1, -2, 2, 0, 0, 1.0f },   /* NOTCH */
    { 1, -3, 6, -4, 0, 1.0f },  /* PHASE */
    { 1, -1, 0, 0, 0, 1.0f },   /* HP1 */
    { 1, -2, 1, 0, 0, 1.0f },   /* HP2 */
    { 1, -3, 3, -1, 0, 1.0f },  /* HP3 */
    { 0, 1, -2, 1, 0, 1.4f },   /* H2+L */
    { 0, 1, -3, 3, -1, 1.4f },  /* H3+L */
    { 0, 1, -2, 2, 0, 1.0f },   /* NO+L */
    { 0, 1, -3, 6, -4, 1.0f },  /* PH+L */
};
static const char *const flt_names[TFM_FLT_MODES] = {
    "LP4", "LP3", "LP2", "LP1", "BP4", "BP2", "NOTCH", "PHASE",
    "HP1", "HP2", "HP3", "H2+L", "H3+L", "NO+L", "PH+L"
};
/* destinazioni delle chain: parametro del preset (255 = pitch) */
#define MOD_PITCH_ID 255u
static const uint8_t mod_param[TFM_MOD_TARGETS] = {
    0, TFM_P_CUTOFF, TFM_P_RES, TFM_P_HEAT, TFM_P_FLT_ENV, TFM_P_WOBBLE,
    TFM_OP_BASE(0) + TFM_OPP_VOL, TFM_OP_BASE(1) + TFM_OPP_VOL, TFM_OP_BASE(2) + TFM_OPP_VOL,
    TFM_OP_BASE(3) + TFM_OPP_VOL, TFM_OP_BASE(4) + TFM_OPP_VOL, TFM_OP_BASE(5) + TFM_OPP_VOL,
    TFM_OP_BASE(0) + TFM_OPP_WAVE, TFM_OP_BASE(1) + TFM_OPP_WAVE, TFM_OP_BASE(2) + TFM_OPP_WAVE,
    TFM_OP_BASE(3) + TFM_OPP_WAVE, TFM_OP_BASE(4) + TFM_OPP_WAVE, TFM_OP_BASE(5) + TFM_OPP_WAVE,
    TFM_OP_BASE(0) + TFM_OPP_MULT, TFM_OP_BASE(1) + TFM_OPP_MULT, TFM_OP_BASE(2) + TFM_OPP_MULT,
    TFM_OP_BASE(3) + TFM_OPP_MULT, TFM_OP_BASE(4) + TFM_OPP_MULT, TFM_OP_BASE(5) + TFM_OPP_MULT,
    TFM_P_FB1, TFM_P_FB2, MOD_PITCH_ID, TFM_P_L2_TUNE, TFM_P_FAT, TFM_P_VOLUME,
    TFM_P_DIST_DRIVE, TFM_P_PH_DEPTH, TFM_P_DLY_TIME, TFM_P_REV_MIX
};
const char *const tfm_mod_target_names[TFM_MOD_TARGETS] = {
    "--", "CUTOFF", "RES", "HEAT", "F.ENV", "WOBBLE",
    "OP1 VOL", "OP2 VOL", "OP3 VOL", "OP4 VOL", "OP5 VOL", "OP6 VOL",
    "OP1 WAV", "OP2 WAV", "OP3 WAV", "OP4 WAV", "OP5 WAV", "OP6 WAV",
    "OP1 MUL", "OP2 MUL", "OP3 MUL", "OP4 MUL", "OP5 MUL", "OP6 MUL",
    "FB 1", "FB 5", "PITCH", "L2 TUNE", "FAT", "VOLUME",
    "DRIVE", "PH DEPT", "DLY TIM", "REV MIX"
};
static const char *const wave_names[8] = {
    "SINE", "HALF", "ABS", "PULS", "EVEN", "EABS", "SQUA", "DSQR"
};

/* Saturazione "zener" asimmetrica per il flavour MS20. */
static TFM_DSP_CODE inline float ms20_clip(float x)
{
    float p = x > 0 ? x / (1.f + 0.35f * x) : x / (1.f - 0.75f * x);
    return p;
}

typedef struct {
    float g0, g1;      /* coefficiente G a inizio e fine blocco */
    float k;           /* feedback */
    float drive, makeup, wobble;
    const float *mix;
    uint8_t ms20;
} flt_ctl;

static TFM_DSP_CODE inline float flt_process(tfm_filter *f, const flt_ctl *c, float G, float x)
{
    /* HEAT: drive analogico in ingresso, piu' un rumore minimo come in un
     * circuito vero (serve anche ad avviare l'autooscillazione) */
    f->noise = f->noise * 1664525u + 1013904223u;
    x = tfm_tanhf(x * c->drive) * c->makeup + (float)(int32_t)f->noise * 2.3e-15f;
    /* Wobble: il primo polo modula il cutoff */
    if (c->wobble != 0.f) {
        G *= 1.f + c->wobble * f->s[0];
        G = tfm_clampf(G, 0.0001f, 0.98f);
    }
    /* Feedback senza ritardo (TPT): y4 = G^4*y0 + S, risolto in forma chiusa
     * sulla parte lineare, poi la non linearita' viene applicata alla stima.
     * Cosi' l'autooscillazione arriva a k = 4 a qualunque cutoff. */
    float H = 1.f - G;
    float S = H * (f->s[3] + G * (f->s[2] + G * (f->s[1] + G * f->s[0])));
    float G2 = G * G, G4 = G2 * G2;
    float comp = 1.f + 0.5f * c->k; /* recupero parziale dei bassi con la risonanza */
    float xin = x * comp;
    float y0lin = (xin - c->k * S) / (1.f + c->k * G4);
    float est = G4 * y0lin + S;
    float y0 = xin - c->k * (c->ms20 ? ms20_clip(est) : tfm_tanhf(est));
    float in = y0, y[4];
    for (int i = 0; i < 4; i++) {
        float v = (in - f->s[i]) * G;
        float o = v + f->s[i];
        f->s[i] = o + v;
        y[i] = o;
        /* MS20: saturazione asimmetrica anche tra il primo e il secondo polo */
        in = (c->ms20 && i == 0) ? ms20_clip(o) : o;
    }
    f->y4 = y[3];
    const float *m = c->mix;
    return (m[0] * y0 + m[1] * y[0] + m[2] * y[1] + m[3] * y[2] + m[4] * y[3]) * m[5];
}

/* ------------------------------------------------------------------ */
/* Inviluppo del filtro (ADSR esponenziale, a rate di blocco)          */
/* ------------------------------------------------------------------ */

typedef struct { float level; uint8_t stage; } tfm_adsr;

/* Fader 0..127 -> coefficiente per blocco di 'n' campioni, tempi 1 ms..10 s */
static float adsr_coef(uint8_t fader, unsigned n)
{
    float t = 0.001f * tfm_exp2f((float)fader * (13.3f / 127.f)); /* 1 ms .. ~10 s */
    float samples = t * (float)TFM_SAMPLE_RATE;
    /* raggiungere ~99% in 't' -> coef = 1 - exp(-4.6 n / samples) */
    float x = 4.6f * (float)n / samples;
    if (x > 10.f) return 1.f;
    return 1.f - tfm_exp2f(-x * 1.442695f);
}

/* ------------------------------------------------------------------ */
/* Voci ed engine                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    tfm_op op[TFM_OPS];
    tfm_filter flt;
    tfm_adsr fenv;
    float g_prev;
    uint32_t age;
    int8_t note;      /* -1 libera */
    uint8_t vel, gate;
    float detune;     /* semitoni (Fat) */
    float pan;        /* 0 = sinistra, 1 = destra */
    float gain;       /* compensazione di volume nei modi a voci sovrapposte */
    tfm_adsr menv;    /* inviluppo di modulazione */
    float pitch_cur;  /* nota che suona ora (glide), in semitoni */
    float rise_off, fall_off;   /* scostamenti di pitch di Rise e Fall */
    uint8_t shed;               /* spenta in fretta per alleggerire la CPU */
    float setup_pitch;          /* pitch e versione dei parametri dell'ultimo voice_setup_ops */
    uint32_t setup_ver;
} tfm_voice;

struct tfm_engine {
    tfm_patch patch;
    tfm_voice v[TFM_VOICES];
    uint32_t clock;
    float dc_x[2], dc_y[2];
    tfm_fx fx;
    int bend_q16;
    uint8_t dirty;
    uint8_t held[16], nheld;   /* tasti tenuti, in ordine (per i modi mono) */
    int8_t mono_note;          /* nota che suona nei modi mono, -1 nessuna */
    uint8_t rr, wide;          /* contatori per ROUND ROBIN e WIDE8 */
    float lfo_ph[TFM_LFOS], lfo_hold[TFM_LFOS];   /* fase e valore campionato (NOIZ) */
    float vib_ph, vib_hold, trem_ph;
    float last_pitch;          /* l'ultima nota suonata: da qui parte il glide */
    uint32_t rnd;
    /* --- fase 5: ingresso delle note (scala, accordo), arpeggiatore, sequencer --- */
    tfm_seq seq;
    uint8_t in_map[128][5];    /* le note che ogni tasto fa suonare (0xff = nessuna) */
    uint8_t arp_held[16], arp_n, arp_keys, arp_vel, arp_restart, arp_par, arp_open;
    uint8_t arp_out[8], arp_nout;
    int16_t arp_walk;
    uint32_t arp_k;
    float arp_t;
    uint8_t seq_run, seq_step, seq_restart, seq_par, seq_open, seq_nout;
    int8_t seq_ping, seq_tr;
    uint8_t seq_out[5];
    float seq_t;
    /* clock: esterno (tick MIDI) o interno; clock out a 24 tick per 1/4 */
    uint8_t ext_sync;
    unsigned clk_out;
    float ext_beats, ext_bpm, clk_ph;
    uint32_t ext_samples;
    int (*par_begin)(void), (*par_end)(void);   /* secondo core (tfm_set_parallel) */
    uint32_t ver;              /* cresce a ogni parametro cambiato */
    float og, og_target;       /* guadagno d'uscita (MASTER), prima del limitatore */
};

unsigned tfm_engine_size(void) { return (sizeof(tfm_engine) + 7u) & ~7u; }

static const uint8_t mult_x2[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
static const uint8_t ksl_rom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
static const uint8_t ksl_shift[4] = { 8, 2, 1, 0 }; /* 0, -1.5, -3, -6 dB/ott */
/* spostamenti di detune per Fat (in frazioni del range) */
static const float fat_spread[TFM_VOICES] = { 0.f, 1.f, -1.f, 0.5f, -0.5f, 0.75f, -0.75f, 0.25f };

static inline unsigned f_to(uint8_t v, unsigned n) { return ((unsigned)v * n) >> 7; } /* 0..127 -> 0..n-1 */

static inline float note_hz(float note)
{
    return 440.f * tfm_exp2f((note - 69.f) * (1.f / 12.f));
}

/* Converte una frequenza nella coppia block/fnum dell'OPL3 (per KSL e KSR). */
static void opl_keycode(float hz, unsigned *block, unsigned *fnum)
{
    unsigned b = 0;
    float fn = hz * 1048576.f / 49716.f; /* fnum a block 0 */
    while (fn >= 1024.f && b < 7) { fn *= 0.5f; b++; }
    if (fn > 1023.f) fn = 1023.f;
    *block = b;
    *fnum = (unsigned)fn;
}

/* p: parametri effettivi (con le modulazioni); base: pitch in semitoni MIDI */
static void voice_setup_ops(tfm_voice *v, int retrigger, const uint8_t *p, float base)
{
    float l2 = (float)((int)p[TFM_P_L2_TUNE] - 64);
    if (l2 > 24.f) l2 = 24.f;
    if (l2 < -24.f) l2 = -24.f;
    for (unsigned i = 0; i < TFM_OPS; i++) {
        tfm_op *o = &v->op[i];
        const uint8_t *q = p + TFM_OP_BASE(i);
        float hz = note_hz(base + (i >= 4 ? l2 : 0.f));
        unsigned block, fnum;
        opl_keycode(hz, &block, &fnum);
        float mult = (float)mult_x2[f_to(q[TFM_OPP_MULT], 16)] * 0.5f;
        float inc = hz * mult * (4294967296.f / (float)TFM_SAMPLE_RATE);
        if (inc > 4.2e9f) inc = 4.2e9f;
        o->inc = (uint32_t)inc;
        o->wave = (uint8_t)f_to(q[TFM_OPP_WAVE], 8);
        /* KSR: keycode = block*2 + bit 9 di fnum */
        unsigned kc = (block << 1) | ((fnum >> 9) & 1);
        unsigned ks = q[TFM_OPP_KSR] ? kc : (kc >> 2);
        /* fader alto = tempo lungo; rate 15 (istantaneo) .. 1 (lentissimo) */
        unsigned ar = 15 - f_to(q[TFM_OPP_A], 15);
        unsigned dr = 15 - f_to(q[TFM_OPP_D], 15);
        unsigned rr = 15 - f_to(q[TFM_OPP_R], 15);
        o->ar = (uint8_t)(ar ? (ar * 4 + ks > 63 ? 63 : ar * 4 + ks) : 0);
        o->dr = (uint8_t)(dr ? (dr * 4 + ks > 63 ? 63 : dr * 4 + ks) : 0);
        o->rr = (uint8_t)(rr ? (rr * 4 + ks > 63 ? 63 : rr * 4 + ks) : 0);
        if (v->shed) o->rr = 60;
        unsigned sl = 15 - f_to(q[TFM_OPP_S], 16);
        o->sl = (sl == 15 ? 0x1f0u : sl << 4) << 16;
        /* attenuazione: TL + KSL + velocity */
        int ksl = ((int)ksl_rom[fnum >> 6] << 2) - ((8 - (int)block) << 5);
        if (ksl < 0) ksl = 0;
        unsigned kslv = q[TFM_OPP_KSL] > 3 ? 3 : q[TFM_OPP_KSL];
        unsigned tl = (127u - q[TFM_OPP_VOL]) >> 1;
        unsigned kvs = q[TFM_OPP_KVS] > 10 ? 10 : q[TFM_OPP_KVS];
        unsigned velatt = (127u - v->vel) * kvs * 192u / 1270u;
        unsigned att = (tl << 2) + ((unsigned)ksl >> ksl_shift[kslv]) + velatt;
        o->att = (uint16_t)(att > 511 || q[TFM_OPP_VOL] == 0 ? 511 : att);
        o->silent = q[TFM_OPP_VOL] == 0;
        if (o->silent) { o->out = o->prev = 0; }
        if (retrigger) {
            o->stage = EG_ATTACK;
            if (o->ar == 0) o->stage = EG_DECAY; /* attack rate 0: niente attacco */
            if (o->ar >= 60) { o->env = 0; o->stage = EG_DECAY; }
            o->phase = 0;
            o->out = o->prev = 0;
        }
    }
}

/* Calcola un campione dei due layer di una voce. Valore ~ +-1 per portante. */
static TFM_DSP_CODE inline int voice_fm(tfm_voice *v, unsigned algo, unsigned fb1, unsigned algo2, unsigned fb2)
{
    tfm_op *o = v->op;
    int m1 = fb1 ? (o[0].prev + o[0].out) >> (9 - fb1) : 0;
    int a = op_calc(&o[0], m1);
    int sum;
    switch (algo) {
    default:
    case 0: { /* 1->2->3->4 */
        int b = op_calc(&o[1], a);
        int c = op_calc(&o[2], b);
        sum = op_calc(&o[3], c);
        break;
    }
    case 1: { /* 1 + (2->3->4) */
        int b = op_calc(&o[1], 0);
        int c = op_calc(&o[2], b);
        sum = a + op_calc(&o[3], c);
        break;
    }
    case 2: { /* (1->2) + (3->4) */
        int b = op_calc(&o[1], a);
        int c = op_calc(&o[2], 0);
        sum = b + op_calc(&o[3], c);
        break;
    }
    case 3: { /* 1 + (2->3) + 4 */
        int b = op_calc(&o[1], 0);
        int c = op_calc(&o[2], b);
        sum = a + c + op_calc(&o[3], 0);
        break;
    }
    case 4: /* 1 + 2 + 3 + 4 */
        sum = a + op_calc(&o[1], 0) + op_calc(&o[2], 0) + op_calc(&o[3], 0);
        break;
    /* --- algoritmi extra (non presenti sulla TWISTfm) --- */
    case 5: { /* (1+2) -> 3 -> 4 */
        int b = op_calc(&o[1], 0);
        int c = op_calc(&o[2], a + b);
        sum = op_calc(&o[3], c);
        break;
    }
    case 6: /* 1 -> (2 + 3 + 4) */
        sum = op_calc(&o[1], a) + op_calc(&o[2], a) + op_calc(&o[3], a);
        break;
    case 7: { /* (1+2+3) -> 4 */
        int b = op_calc(&o[1], 0);
        int c = op_calc(&o[2], 0);
        sum = op_calc(&o[3], a + b + c);
        break;
    }
    case 8: { /* 1 -> 2 -> (3 + 4) */
        int b = op_calc(&o[1], a);
        sum = op_calc(&o[2], b) + op_calc(&o[3], b);
        break;
    }
    case 9: /* (1->2) + 3 + 4 */
        sum = op_calc(&o[1], a) + op_calc(&o[2], 0) + op_calc(&o[3], 0);
        break;
    }
    int m5 = fb2 ? (o[4].prev + o[4].out) >> (9 - fb2) : 0;
    int e5 = op_calc(&o[4], m5);
    if (algo2) sum += e5 + op_calc(&o[5], 0);
    else sum += op_calc(&o[5], e5);
    return sum;
}

/* Convertitore Yamaha YAC512 (uno per voce nella TWISTfm): virgola mobile
 * con mantissa a 10 bit ed esponente a 3 bit, dopo la somma a 16 bit del chip.
 * I segnali forti perdono i bit bassi: e' la "grana" tipica dei chip Yamaha. */
static TFM_DSP_CODE inline int yac512(int v)
{
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    unsigned a = (unsigned)(v < 0 ? ~v : v);
    unsigned shift = 0;
    while (a >= 512u) { a >>= 1; shift++; }
    return shift ? (int)((unsigned)v & ~((1u << shift) - 1u)) : v;
}

/* Operatori che arrivano all'uscita, per sapere quando la voce tace. */
static const uint8_t carriers[TFM_ALGOS] = { 0x08, 0x09, 0x0a, 0x0d, 0x0f, 0x08, 0x0e, 0x08, 0x0c, 0x0e };

static int voice_silent(const tfm_voice *v, unsigned algo, unsigned algo2)
{
    unsigned mask = carriers[algo] | (algo2 ? 0x30u : 0x20u);
    for (unsigned i = 0; i < TFM_OPS; i++)
        if ((mask >> i) & 1u)
            if (v->op[i].stage != EG_OFF && !v->op[i].silent
                && (v->op[i].stage == EG_ATTACK || (v->op[i].env >> 16) + v->op[i].att < 400)) return 0;   /* sotto ~-75 dB la voce e' finita */
    return 1;
}

void tfm_patch_init(tfm_patch *patch)
{
    tfm_memset(patch, 0, sizeof *patch);
    uint8_t *p = patch->p;
    for (unsigned i = 0; i < TFM_OPS; i++) {
        uint8_t *q = p + TFM_OP_BASE(i);
        q[TFM_OPP_VOL] = (i == 3 || i == 5) ? 127 : 0; /* solo le portanti suonano */
        q[TFM_OPP_WAVE] = 0;
        q[TFM_OPP_MULT] = 8;      /* x1 */
        q[TFM_OPP_A] = 0;
        q[TFM_OPP_D] = 64;
        q[TFM_OPP_S] = 127;
        q[TFM_OPP_R] = 48;
        q[TFM_OPP_KVS] = 5;
    }
    p[TFM_P_ALGO] = 0;
    p[TFM_P_ALGO2] = 0;
    p[TFM_P_L2_TUNE] = 64;
    p[TFM_P_FLT_TYPE] = 0;
    p[TFM_P_CUTOFF] = 127;
    p[TFM_P_RES] = 0;
    p[TFM_P_FLT_S] = 127;
    p[TFM_P_FLT_R] = 40;
    p[TFM_P_FLAVOUR] = 1;    /* MS20 */
    p[TFM_P_VOLUME] = 100;
    p[TFM_P_VOICES] = 8;
    p[TFM_P_PRIO] = 0;
    /* effetti: valori di partenza sensati, tutti spenti (MIX = 0) */
    p[TFM_P_DIST_TYPE] = 0; p[TFM_P_DIST_DRIVE] = 45; p[TFM_P_DIST_TEMP] = 60;
    p[TFM_P_PH_RATE] = 40; p[TFM_P_PH_DEPTH] = 90; p[TFM_P_PH_FB] = 50;
    p[TFM_P_DLY_TIME] = 80; p[TFM_P_DLY_FB] = 55; p[TFM_P_DLY_TONE] = 70;
    p[TFM_P_DLY_WOW] = 40; p[TFM_P_DLY_SAT] = 45; p[TFM_P_DLY_SPREAD] = 40;
    p[TFM_P_REV_TYPE] = 1; p[TFM_P_REV_DECAY] = 70; p[TFM_P_REV_TONE] = 70;
    p[TFM_P_REV_PREDLY] = 10; p[TFM_P_REV_MOD] = 40; p[TFM_P_REV_SHIM] = 80;
    /* catena di partenza DIST > PHASER > TAPE > REVERB, tutti con MIX a 0 */
    p[TFM_P_FX_SLOT1] = 1; p[TFM_P_FX_SLOT2] = 2; p[TFM_P_FX_SLOT3] = 3; p[TFM_P_FX_SLOT4] = 4;
    /* modulazioni: tutte scollegate (quantita' a zero) */
    for (unsigned k = 0; k < TFM_LFOS; k++) {
        p[TFM_LFO_BASE(k)] = (uint8_t)(50 + 15 * k);
        for (unsigned i = 0; i < TFM_LINKS; i++) p[TFM_LFO_LINK(k, i) + 1] = TFM_AMT_ZERO;
    }
    for (unsigned i = 0; i < TFM_LINKS; i++) p[TFM_MENV_LINK(i) + 1] = TFM_AMT_ZERO;
    p[TFM_P_MENV_D] = 64; p[TFM_P_MENV_R] = 40;
    p[TFM_P_VIB_RATE] = 70;
    p[TFM_P_TREM_RATE] = 60; p[TFM_P_TREM_PHASE] = 64;
    /* fase 5: 120 BPM, arp e sequencer a 1/16, sequenza di 16 passi */
    p[TFM_P_BPM] = 60;
    p[TFM_P_ARP_RATE] = 9; p[TFM_P_ARP_GATE] = 80;
    p[TFM_P_SEQ_LEN] = 15; p[TFM_P_SEQ_RATE] = 9; p[TFM_P_SEQ_GATE] = 90;
    p[TFM_P_GLIDE] = 40; p[TFM_P_BEND] = 2; p[TFM_P_DLY_DIV] = 7;
    p[TFM_P_LFO1_DIV] = 5; p[TFM_P_LFO2_DIV] = 7; p[TFM_P_LFO3_DIV] = 3;   /* 1/4, 1/8, 1/2 */   /* portamento spento ma pronto; eco 1/8 puntato */
}

static void player_reset(tfm_engine *e);
static void voice_off(tfm_engine *e, uint8_t note);
static void release_list(tfm_engine *e, uint8_t *list, uint8_t *n);
tfm_engine *tfm_engine_init(void *memory)
{
    tfm_engine *e = (tfm_engine *)memory;
    tfm_memset(e, 0, sizeof *e);
    for (unsigned i = 0; i < TFM_VOICES; i++) {
        e->v[i].note = -1;
        for (unsigned k = 0; k < TFM_OPS; k++) e->v[i].op[k].env = EG_MAX;
    }
    e->mono_note = -1;
    e->rnd = 2463534242u;   /* seme del generatore casuale */
    player_reset(e);
    tfm_seq_default(&e->seq);
    e->og = e->og_target = 1.f;
    tfm_fx_init(&e->fx);
    tfm_patch_init(&e->patch);
    return e;
}

void tfm_load_patch(tfm_engine *e, const tfm_patch *patch) { e->patch = *patch; e->dirty = 1; e->ver++; }
void tfm_set_param(tfm_engine *e, unsigned id, uint8_t value)
{
    if (id >= TFM_P_COUNT) return;
    if (value > 127) value = 127;
    if ((id == TFM_P_VOICE_MODE || id == TFM_P_VOICES) && e->patch.p[id] != value) tfm_all_notes_off(e);
    if (id == TFM_P_ARP_ON && e->patch.p[id] != value) {   /* l'arp cambia stato: le note tenute ripartono pulite */
        release_list(e, e->arp_out, &e->arp_nout);
        e->arp_open = 0;
        if (!value) e->arp_n = 0;
        for (unsigned k = 0; k < 128; k++)
            if (e->in_map[k][0] != 0xff) {
                for (unsigned i = 0; i < 5 && e->in_map[k][i] != 0xff; i++) voice_off(e, e->in_map[k][i]);
                tfm_memset(e->in_map[k], 0xff, 5);
            }
        e->arp_keys = 0;
    }
    if (id == TFM_P_ARP_LATCH && !value && e->patch.p[id] && !e->arp_keys) {   /* latch spento senza tasti: stop */
        e->arp_n = 0;
        release_list(e, e->arp_out, &e->arp_nout);
        e->arp_open = 0;
    }
    e->patch.p[id] = value;
    e->ver++;
    e->dirty = 1;
}
uint8_t tfm_get_param(const tfm_engine *e, unsigned id) { return id < TFM_P_COUNT ? e->patch.p[id] : 0; }
void tfm_pitch_bend(tfm_engine *e, int q16) { e->bend_q16 = q16; }
/* CPU al limite: la voce rilasciata piu' vecchia si spegne in pochi millisecondi */
void tfm_shed_voice(tfm_engine *e)
{
    int best = -1;
    uint32_t oldest = UINT32_MAX;
    for (unsigned i = 0; i < TFM_VOICES; i++) {
        tfm_voice *v = &e->v[i];
        if (v->note >= 0 && !v->gate && v->age < oldest) { oldest = v->age; best = (int)i; }
    }
    if (best < 0) return;
    e->v[best].shed = 1;
    for (unsigned k = 0; k < TFM_OPS; k++) {
        tfm_op *o = &e->v[best].op[k];
        if (o->stage != EG_OFF) { o->stage = EG_RELEASE; o->rr = 60; }
    }
}
void tfm_set_out_gain(tfm_engine *e, float g) { e->og_target = g < 0.f ? 0.f : g > 8.f ? 8.f : g; }
void tfm_set_sync(tfm_engine *e, int ext) { e->ext_sync = ext ? 1 : 0; }
void tfm_midi_clock(tfm_engine *e)
{
    if (e->ext_samples > 0 && e->ext_samples < TFM_SAMPLE_RATE) {   /* BPM misurato, levigato */
        float bpm = 60.f * (float)TFM_SAMPLE_RATE / ((float)e->ext_samples * 24.f);
        e->ext_bpm = e->ext_bpm > 0.f ? e->ext_bpm + (bpm - e->ext_bpm) * 0.1f : bpm;
    }
    e->ext_samples = 0;
    e->ext_beats += 1.f / 24.f;
}
unsigned tfm_clock_out(tfm_engine *e) { unsigned n = e->clk_out; e->clk_out = 0; return n; }
unsigned tfm_bpm_now(const tfm_engine *e)
{
    return e->ext_sync && e->ext_bpm > 0.f ? (unsigned)(e->ext_bpm + 0.5f) : 60u + e->patch.p[TFM_P_BPM];
}
const char *tfm_filter_mode_name(unsigned m) { return m < TFM_FLT_MODES ? flt_names[m] : "?"; }
const char *tfm_wave_name(unsigned w) { return wave_names[w & 7]; }

static unsigned algo_of(const tfm_engine *e)
{
    unsigned a = e->patch.p[TFM_P_ALGO];
    return a < TFM_ALGOS ? a : f_to((uint8_t)a, TFM_ALGOS);
}
static unsigned flt_mode_of(const tfm_engine *e)
{
    unsigned m = e->patch.p[TFM_P_FLT_TYPE];
    return m < TFM_FLT_MODES ? m : f_to((uint8_t)m, TFM_FLT_MODES);
}

static float bend_semis(const tfm_engine *e)
{
    unsigned r = e->patch.p[TFM_P_BEND];
    return (float)e->bend_q16 * (1.f / 65536.f) * (float)(r < 1 ? 1 : r > 24 ? 24 : r);
}

static void start_voice(tfm_engine *e, tfm_voice *v, uint8_t note, uint8_t vel, float detune, float pan, int glide)
{
    const uint8_t *p = e->patch.p;
    v->note = (int8_t)note;
    v->vel = vel;
    v->gate = 1;
    v->age = ++e->clock;
    v->detune = detune;
    v->pan = pan;
    v->fenv.stage = 1;
    v->fenv.level = 0.f;
    v->menv.stage = 1;
    v->menv.level = 0.f;
    v->shed = 0;
    /* glide: parte dall'ultima nota suonata */
    v->pitch_cur = glide ? e->last_pitch : (float)note;
    v->rise_off = -(float)p[TFM_P_RISE] * (12.f / 127.f);
    v->fall_off = 0.f;
    for (unsigned k = 0; k < TFM_LFOS; k++)            /* retrigger degli LFO */
        if (p[TFM_LFO_BASE(k) + 3]) e->lfo_ph[k] = 0.f;
    voice_setup_ops(v, 1, p, v->pitch_cur + detune + bend_semis(e) + v->rise_off);
}

static void release_voice(tfm_voice *v)
{
    v->gate = 0;
    v->fenv.stage = 3;
    v->menv.stage = 3;
    for (unsigned k = 0; k < TFM_OPS; k++)
        if (v->op[k].stage != EG_OFF) v->op[k].stage = EG_RELEASE;
}

/* Organizzazione delle voci per ogni modo (come la TWISTfm):
 *   POL8   8 voci singole (limitabili con VOICES)
 *   UNIS   tutte le 8 voci sulla stessa nota, monofonico
 *   4X2    4 note, ognuna su una coppia stereo
 *   2X4    2 note, ognuna su un gruppo di 4 voci
 *   MONO   monofonico, una voce per chip (sinistra + destra)
 *   WIDE8  voci singole alternate a sinistra e destra (ping-pong)
 *   RRBN   voci usate a turno 1,2,3...8 */
typedef struct { unsigned group, slots; } voice_layout;
static unsigned mode_of(const tfm_engine *e) { unsigned m = e->patch.p[TFM_P_VOICE_MODE]; return m < TFM_VOICE_MODES ? m : 0; }
static unsigned voices_of(const tfm_engine *e) { unsigned n = e->patch.p[TFM_P_VOICES]; return n < 1 || n > 8 ? 8 : n; }
static voice_layout layout_of(const tfm_engine *e)
{
    voice_layout l = { 1, voices_of(e) };
    switch (mode_of(e)) {
    case 1: l.group = voices_of(e); l.slots = 1; break;   /* UNIS: quante voci all'unisono (VOICES) */
    case 2: l.group = 2; l.slots = 4; break;
    case 3: l.group = 4; l.slots = 2; break;
    case 4: l.group = 2; l.slots = 1; break;
    default: break;
    }
    return l;
}

/* il glide vale per questa nota? ALL: sempre; LEGATO: solo se un'altra nota e' tenuta */
static int glide_now(const tfm_engine *e)
{
    if (!e->patch.p[TFM_P_PORTA] || !e->patch.p[TFM_P_GLIDE] || e->last_pitch <= 0.f) return 0;
    if (!e->patch.p[TFM_P_GLIDE_MODE]) return 1;
    for (unsigned i = 0; i < TFM_VOICES; i++) if (e->v[i].gate) return 1;
    return 0;
}

/* Fa suonare 'note' sullo slot indicato (gruppo di voci). */
static void play_slot(tfm_engine *e, const voice_layout *l, unsigned slot, uint8_t note, uint8_t vel)
{
    float fat = (float)e->patch.p[TFM_P_FAT] * (0.5f / 127.f); /* fino a +-0.5 semitoni */
    unsigned mode = mode_of(e);
    int glide = glide_now(e);
    for (unsigned i = 0; i < l->group; i++) {
        unsigned vi = slot * l->group + i;
        float pan;
        if (l->group > 1) pan = l->group == 2 ? (i ? 0.9f : 0.1f) : 0.1f + 0.8f * (float)i / (float)(l->group - 1);
        else if (mode == 5) { pan = (e->wide & 1) ? 0.95f : 0.05f; }
        else pan = (vi & 1) ? 0.35f : 0.65f;
        start_voice(e, &e->v[vi], note, vel, fat * fat_spread[l->group > 1 ? i : vi], pan, glide);
        e->v[vi].gain = l->group == 1 ? 1.f : l->group == 2 ? 0.71f : l->group == 4 ? 0.5f : 0.36f;
    }
    if (mode == 5) e->wide++;
    e->last_pitch = (float)note;
}

/* Cambia nota a uno slot senza riattaccare gli inviluppi (legato mono). */
static void glide_slot(tfm_engine *e, const voice_layout *l, unsigned slot, uint8_t note)
{
    for (unsigned i = 0; i < l->group; i++) {
        tfm_voice *v = &e->v[slot * l->group + i];
        v->note = (int8_t)note;
        if (!e->patch.p[TFM_P_PORTA] || !e->patch.p[TFM_P_GLIDE]) v->pitch_cur = (float)note;   /* senza portamento: salto immediato */
    }
    e->last_pitch = (float)note;
}

static int mono_target(const tfm_engine *e)
{
    if (!e->nheld) return -1;
    unsigned prio = e->patch.p[TFM_P_PRIO];
    int best = e->held[e->nheld - 1];
    for (unsigned i = 0; i < e->nheld; i++) {
        if (prio == 1 && e->held[i] > best) best = e->held[i];
        if (prio == 2 && e->held[i] < best) best = e->held[i];
    }
    return best;
}

static void voice_on(tfm_engine *e, uint8_t note, uint8_t velocity)
{
    if (velocity == 0) { voice_off(e, note); return; }
    voice_layout l = layout_of(e);
    if (l.slots == 1) { /* modi monofonici: pila delle note tenute */
        unsigned j = 0;
        for (unsigned i = 0; i < e->nheld; i++) if (e->held[i] != note) e->held[j++] = e->held[i];
        e->nheld = (uint8_t)j;
        if (e->nheld < 16) e->held[e->nheld++] = note;
        int t = mono_target(e);
        if (e->mono_note < 0 || !e->v[0].gate) play_slot(e, &l, 0, (uint8_t)t, velocity);
        else if (t != e->mono_note) glide_slot(e, &l, 0, (uint8_t)t);
        e->mono_note = (int8_t)t;
        return;
    }
    unsigned algo = algo_of(e), algo2 = e->patch.p[TFM_P_ALGO2] ? 1 : 0;
    int best = -1;
    if (mode_of(e) == 6) { /* ROUND ROBIN: sempre la voce successiva */
        best = (int)(e->rr % l.slots);
        e->rr = (uint8_t)((e->rr + 1) % l.slots);
    }
    /* 1) stessa nota, 2) slot silenzioso, 3) rilasciato piu' vecchio, 4) piu' vecchio */
    for (unsigned s = 0; s < l.slots && best < 0; s++)
        if (e->v[s * l.group].note == (int8_t)note) best = (int)s;
    for (unsigned s = 0; s < l.slots && best < 0; s++) {
        tfm_voice *v = &e->v[s * l.group];
        if (v->note < 0 || voice_silent(v, algo, algo2)) best = (int)s;
    }
    if (best < 0) {
        uint32_t oldest = UINT32_MAX;
        for (int pass = 0; pass < 2 && best < 0; pass++) {
            int pick = -1;
            for (unsigned s = 0; s < l.slots; s++) {
                tfm_voice *v = &e->v[s * l.group];
                if ((pass || !v->gate) && v->age < oldest) { oldest = v->age; pick = (int)s; }
            }
            best = pick;
        }
    }
    /* le voci oltre il limite di polifonia vengono spente */
    for (unsigned vi = l.slots * l.group; vi < TFM_VOICES; vi++) if (e->v[vi].gate) release_voice(&e->v[vi]);
    play_slot(e, &l, (unsigned)best, note, velocity);
}

static void voice_off(tfm_engine *e, uint8_t note)
{
    voice_layout l = layout_of(e);
    if (l.slots == 1) {
        unsigned j = 0;
        for (unsigned i = 0; i < e->nheld; i++) if (e->held[i] != note) e->held[j++] = e->held[i];
        e->nheld = (uint8_t)j;
        int t = mono_target(e);
        if (t < 0) {
            for (unsigned i = 0; i < l.group; i++) if (e->v[i].gate) release_voice(&e->v[i]);
            e->mono_note = -1;
        } else if (t != e->mono_note) {
            glide_slot(e, &l, 0, (uint8_t)t);
            e->mono_note = (int8_t)t;
        }
        return;
    }
    for (unsigned i = 0; i < TFM_VOICES; i++)
        if (e->v[i].gate && e->v[i].note == (int8_t)note) release_voice(&e->v[i]);
}

void tfm_all_notes_off(tfm_engine *e)
{
    player_reset(e);
    e->nheld = 0;
    e->mono_note = -1;
    for (unsigned i = 0; i < TFM_VOICES; i++) if (e->v[i].gate) release_voice(&e->v[i]);
}

/* =================== fase 5: scale, accordi, arpeggiatore, sequencer =================== */

const char *const tfm_div_names[TFM_DIVS] = { "4BAR", "2BAR", "1BAR", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32" };
const float tfm_div_beats[TFM_DIVS] = { 16.f, 8.f, 4.f, 2.f, 4.f / 3.f, 1.f, 2.f / 3.f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f };
const char *const tfm_dly_div_names[TFM_DLY_DIVS] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2" };
const float tfm_dly_div_beats[TFM_DLY_DIVS] = { 0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.375f, 0.5f, 2.f / 3.f, 0.75f, 1.f, 1.5f, 2.f };
const char *const tfm_arp_names[TFM_ARP_MODES] = { "UP", "DOWN", "UPDN", "RAND", "WALK", "CONV", "DIVG", "PLAY", "STUT", "ALL" };
const char *const tfm_scale_names[TFM_SCALES] = {
    "CHRO", "MAJR", "MINR", "DORI", "PHRY", "LYDI", "MIXO", "LOCR", "HARm", "MELm", "PNTM",
    "PNTm", "BLUE", "GYPS", "HUNG", "DIMI", "DEBU", "NEAP", "DBLH", "ALTR", "PERS", "ENIG" };
#define SC(a, b, c, d, e2, f, g, h, i, j, k, l) (uint16_t)((a) | (b) << 1 | (c) << 2 | (d) << 3 | (e2) << 4 | (f) << 5 | (g) << 6 | (h) << 7 | (i) << 8 | (j) << 9 | (k) << 10 | (l) << 11)
const uint16_t tfm_scale_masks[TFM_SCALES] = {
    SC(1,1,1,1,1,1,1,1,1,1,1,1),   /* cromatica */
    SC(1,0,1,0,1,1,0,1,0,1,0,1),   /* maggiore */
    SC(1,0,1,1,0,1,0,1,1,0,1,0),   /* minore naturale */
    SC(1,0,1,1,0,1,0,1,0,1,1,0),   /* dorica */
    SC(1,1,0,1,0,1,0,1,1,0,1,0),   /* frigia */
    SC(1,0,1,0,1,0,1,1,0,1,0,1),   /* lidia */
    SC(1,0,1,0,1,1,0,1,0,1,1,0),   /* misolidia */
    SC(1,1,0,1,0,1,1,0,1,0,1,0),   /* locria */
    SC(1,0,1,1,0,1,0,1,1,0,0,1),   /* minore armonica */
    SC(1,0,1,1,0,1,0,1,0,1,0,1),   /* minore melodica */
    SC(1,0,1,0,1,0,0,1,0,1,0,0),   /* pentatonica maggiore */
    SC(1,0,0,1,0,1,0,1,0,0,1,0),   /* pentatonica minore */
    SC(1,0,0,1,0,1,1,1,0,0,1,0),   /* blues */
    SC(1,1,0,0,1,1,0,1,1,0,1,0),   /* gypsy (frigia dominante) */
    SC(1,0,1,1,0,0,1,1,1,0,0,1),   /* ungherese minore */
    SC(1,0,1,1,0,1,1,0,1,1,0,1),   /* diminuita (tono-semitono) */
    SC(1,0,1,0,1,0,1,0,1,0,1,0),   /* esatonale (Debussy) */
    SC(1,1,0,1,0,1,0,1,0,1,0,1),   /* napoletana maggiore */
    SC(1,1,0,0,1,1,0,1,1,0,0,1),   /* doppia armonica */
    SC(1,1,0,1,1,0,1,0,1,0,1,0),   /* alterata */
    SC(1,1,0,0,1,1,1,0,1,0,0,1),   /* persiana */
    SC(1,1,0,0,1,0,1,0,1,0,1,1) }; /* enigmatica */
const char *const tfm_chord_names[TFM_CHORDS] = { "OFF", "MIN", "MIN7", "MIN9", "MAJ", "MAJ7", "MAJ9", "7SUS", "HOUS", "PWR", "DEBU", "NOIR" };
const int8_t tfm_chord_notes[TFM_CHORDS][5] = {
    { 0, -1, -1, -1, -1 }, { 0, 3, 7, -1, -1 }, { 0, 3, 7, 10, -1 }, { 0, 3, 7, 10, 14 },
    { 0, 4, 7, -1, -1 }, { 0, 4, 7, 11, -1 }, { 0, 4, 7, 11, 14 }, { 0, 5, 7, 10, -1 },
    { 0, 7, 10, 15, -1 }, { 0, 7, 12, -1, -1 }, { 0, 4, 7, 11, 18 }, { 0, 3, 7, 11, 14 } };

void tfm_seq_default(tfm_seq *s)
{
    static const uint8_t n[16] = { 48, 48, 60, 48, 51, 48, 55, 48, 48, 60, 58, 48, 55, 53, 51, 46 };
    static const uint8_t g[16] = { 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1 };
    for (unsigned i = 0; i < TFM_SEQ_STEPS; i++) {
        s->note[i] = i < 16 ? n[i] : 48;
        s->gate[i] = i < 16 ? g[i] : TFM_GATE_TRIG;
    }
}

static unsigned rnd_u(tfm_engine *e, unsigned n) { e->rnd = e->rnd * 1664525u + 1013904223u; return (e->rnd >> 16) % n; }

/* la nota della scala piu' vicina, scendendo */
static int quantize(const tfm_engine *e, int note)
{
    unsigned sc = e->patch.p[TFM_P_SCALE] % TFM_SCALES;
    if (!sc) return note;
    uint16_t m = tfm_scale_masks[sc];
    int rel = (note - (int)(e->patch.p[TFM_P_ROOT] % 12) + 120) % 12;
    for (int d = 0; d < 12; d++)
        if (m & (1u << ((rel - d + 12) % 12))) return note - d;
    return note;
}
/* tasto -> note da suonare: scala e accordo; restituisce quante */
static unsigned expand(const tfm_engine *e, int note, uint8_t out[5])
{
    const int8_t *iv = tfm_chord_notes[e->patch.p[TFM_P_CHORD] % TFM_CHORDS];
    int root = quantize(e, note);
    unsigned n = 0;
    for (unsigned i = 0; i < 5 && iv[i] >= 0; i++) {
        int v = i ? quantize(e, root + iv[i]) : root;
        if (v < 0 || v > 127) continue;
        unsigned dup = 0;
        for (unsigned j = 0; j < n; j++) if (out[j] == v) dup = 1;
        if (!dup) out[n++] = (uint8_t)v;
    }
    for (unsigned i = n; i < 5; i++) out[i] = 0xff;
    return n;
}

static void release_list(tfm_engine *e, uint8_t *list, uint8_t *n)
{
    for (unsigned i = 0; i < *n; i++) voice_off(e, list[i]);
    *n = 0;
}

static void player_reset(tfm_engine *e)
{
    tfm_memset(e->in_map, 0xff, sizeof e->in_map);
    e->arp_n = e->arp_keys = 0;
    e->arp_nout = 0;
    e->arp_open = 0;
    e->seq_nout = 0;
    e->seq_open = 0;
}

void tfm_note_on(tfm_engine *e, uint8_t note, uint8_t velocity)
{
    const uint8_t *p = e->patch.p;
    if (velocity == 0) { tfm_note_off(e, note); return; }
    if (note > 127) return;
    if (e->seq_run && p[TFM_P_SEQ_NOTE]) {   /* sequencer RELA: il tasto traspone la sequenza */
        e->seq_tr = (int8_t)((int)note - 60);
        return;
    }
    if (e->in_map[note][0] != 0xff) tfm_note_off(e, note);
    unsigned n = expand(e, note, e->in_map[note]);
    if (p[TFM_P_ARP_ON]) {
        if (p[TFM_P_ARP_LATCH] && e->arp_keys == 0) { e->arp_n = 0; }   /* latch: un accordo nuovo sostituisce il vecchio */
        if (e->arp_n == 0) e->arp_restart = 1;
        e->arp_keys++;
        e->arp_vel = velocity;
        for (unsigned i = 0; i < n; i++) {
            unsigned dup = 0;
            for (unsigned j = 0; j < e->arp_n; j++) if (e->arp_held[j] == e->in_map[note][i]) dup = 1;
            if (!dup && e->arp_n < 16) e->arp_held[e->arp_n++] = e->in_map[note][i];
        }
        return;
    }
    for (unsigned i = 0; i < n; i++) voice_on(e, e->in_map[note][i], velocity);
}

void tfm_note_off(tfm_engine *e, uint8_t note)
{
    if (note > 127 || e->in_map[note][0] == 0xff) return;
    uint8_t *m = e->in_map[note];
    if (e->patch.p[TFM_P_ARP_ON]) {
        if (e->arp_keys) e->arp_keys--;
        if (!e->patch.p[TFM_P_ARP_LATCH]) {
            for (unsigned i = 0; i < 5 && m[i] != 0xff; i++) {
                unsigned j = 0;
                for (unsigned k = 0; k < e->arp_n; k++) if (e->arp_held[k] != m[i]) e->arp_held[j++] = e->arp_held[k];
                e->arp_n = (uint8_t)j;
            }
            if (!e->arp_n) { release_list(e, e->arp_out, &e->arp_nout); e->arp_open = 0; }
        }
    } else {
        for (unsigned i = 0; i < 5 && m[i] != 0xff; i++) voice_off(e, m[i]);
    }
    tfm_memset(m, 0xff, 5);
}

/* un passo dell'arpeggiatore */
static void arp_step(tfm_engine *e)
{
    const uint8_t *p = e->patch.p;
    release_list(e, e->arp_out, &e->arp_nout);
    unsigned n = e->arp_n;
    if (!n) return;
    uint8_t s[16];
    for (unsigned i = 0; i < n; i++) {   /* ordinate per altezza */
        uint8_t v = e->arp_held[i];
        unsigned j = i;
        while (j && s[j - 1] > v) { s[j] = s[j - 1]; j--; }
        s[j] = v;
    }
    unsigned R = (unsigned)(p[TFM_P_ARP_RANGE] % 6) + 1, T = n * R, k = e->arp_k++, i = 0;
    const uint8_t *src = s;
    switch (p[TFM_P_ARP_MODE] % TFM_ARP_MODES) {
    case 0: i = k % T; break;
    case 1: i = T - 1 - k % T; break;
    case 2: { unsigned per = T > 1 ? 2 * T - 2 : 1, j = k % per; i = j < T ? j : per - j; } break;
    case 3: i = rnd_u(e, T); break;
    case 4:
        e->arp_walk += rnd_u(e, 2) ? 1 : -1;
        if (e->arp_walk < 0) e->arp_walk = T > 1 ? 1 : 0;
        if (e->arp_walk >= (int)T) e->arp_walk = (int16_t)(T > 1 ? T - 2 : 0);
        i = (unsigned)e->arp_walk;
        break;
    case 5: { unsigned j = k % T; i = j & 1 ? T - 1 - j / 2 : j / 2; } break;
    case 6: { unsigned j = T - 1 - k % T; i = j & 1 ? T - 1 - j / 2 : j / 2; } break;
    case 7: i = k % T; src = e->arp_held; break;
    case 8: i = (k / 2) % T; break;
    default: {   /* ALL: tutte le note insieme, un'ottava per passo */
        unsigned o = k % R;
        for (unsigned j = 0; j < n && j < 8; j++) {
            unsigned v = s[j] + 12 * o;
            if (v <= 127) e->arp_out[e->arp_nout++] = (uint8_t)v;
        }
    } break;
    }
    if ((p[TFM_P_ARP_MODE] % TFM_ARP_MODES) != 9) {
        unsigned v = src[i % n] + 12 * (i / n);
        if (v <= 127) e->arp_out[e->arp_nout++] = (uint8_t)v;
    }
    for (unsigned j = 0; j < e->arp_nout; j++) voice_on(e, e->arp_out[j], e->arp_vel);
    e->arp_open = 1;
}

static unsigned seq_len(const tfm_engine *e) { return (unsigned)(e->patch.p[TFM_P_SEQ_LEN] & 127) + 1; }
/* il passo dopo 'cur' (peek: senza cambiare lo stato; RAND non si puo' prevedere) */
static unsigned seq_next(tfm_engine *e, unsigned cur, int peek)
{
    unsigned len = seq_len(e);
    if (cur >= len) cur = len - 1;
    switch (e->patch.p[TFM_P_SEQ_DIR] % 4) {
    case 1: return cur ? cur - 1 : len - 1;
    case 2: {
        int d = e->seq_ping ? e->seq_ping : 1;
        if ((int)cur + d < 0 || (int)cur + d >= (int)len) d = -d;
        if (!peek) e->seq_ping = (int8_t)d;
        int nx = (int)cur + d;
        return nx < 0 || nx >= (int)len ? 0u : (unsigned)nx;
    }
    case 3: return peek ? (cur + 1) % len : rnd_u(e, len);
    default: return (cur + 1) % len;
    }
}
static void seq_play_step(tfm_engine *e, unsigned st)
{
    uint8_t g = e->seq.gate[st];
    int note = (int)e->seq.note[st] + (e->patch.p[TFM_P_SEQ_NOTE] ? e->seq_tr : 0);
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (g == TFM_GATE_REST) { release_list(e, e->seq_out, &e->seq_nout); e->seq_open = 0; return; }
    uint8_t nw[5];
    unsigned n = expand(e, note, nw);
    if (g == TFM_GATE_TIE && e->seq_nout) {   /* legato: la nota nuova prima di lasciare la vecchia */
        if (n && nw[0] == e->seq_out[0]) return;
        uint8_t old[5], no = e->seq_nout;
        for (unsigned i = 0; i < no; i++) old[i] = e->seq_out[i];
        for (unsigned i = 0; i < n; i++) voice_on(e, nw[i], 100);
        for (unsigned i = 0; i < no; i++) {
            unsigned keep = 0;
            for (unsigned j = 0; j < n; j++) if (nw[j] == old[i]) keep = 1;
            if (!keep) voice_off(e, old[i]);
        }
    } else {
        release_list(e, e->seq_out, &e->seq_nout);
        for (unsigned i = 0; i < n; i++) voice_on(e, nw[i], 100);
    }
    for (unsigned i = 0; i < n; i++) e->seq_out[i] = nw[i];
    e->seq_nout = (uint8_t)n;
    e->seq_open = 1;
}

/* orologio: avanza arpeggiatore e sequencer di n campioni */
static void player_tick(tfm_engine *e, unsigned n)
{
    const uint8_t *p = e->patch.p;
    float beats = (float)n * (60.f + (float)p[TFM_P_BPM]) * (1.f / 60.f) / (float)TFM_SAMPLE_RATE;
    if (e->ext_samples < 0x7fffffffu) e->ext_samples += n;
    if (e->ext_sync) { beats = e->ext_beats; e->ext_beats = 0.f; }   /* il tempo lo danno i tick MIDI */
    else {   /* clock out: 24 tick per nota da 1/4 */
        e->clk_ph += beats * 24.f;
        while (e->clk_ph >= 1.f) { e->clk_ph -= 1.f; if (e->clk_out < 64) e->clk_out++; }
    }
    float sw = (float)p[TFM_P_SWING] * (0.33f / 127.f);
    if (p[TFM_P_ARP_ON] && e->arp_n) {
        float base = tfm_div_beats[p[TFM_P_ARP_RATE] % TFM_DIVS];
        float len = base * (e->arp_par ? 1.f - sw : 1.f + sw);
        if (e->arp_restart) { e->arp_restart = 0; e->arp_t = 0.f; e->arp_par = 0; e->arp_k = 0; e->arp_walk = 0; arp_step(e); }
        else {
            e->arp_t += beats;
            if (e->arp_t >= len) { e->arp_t -= len; if (e->arp_t > len) e->arp_t = 0.f; e->arp_par ^= 1u; arp_step(e); }
        }
        len = base * (e->arp_par ? 1.f - sw : 1.f + sw);
        float g = 0.05f + (float)p[TFM_P_ARP_GATE] * (0.95f / 127.f);
        if (e->arp_open && g < 0.99f && e->arp_t >= g * len) { release_list(e, e->arp_out, &e->arp_nout); e->arp_open = 0; }
    }
    if (e->seq_run) {
        float base = tfm_div_beats[p[TFM_P_SEQ_RATE] % TFM_DIVS];
        float len = base * (e->seq_par ? 1.f - sw : 1.f + sw);
        if (e->seq_restart) {
            e->seq_restart = 0; e->seq_t = 0.f; e->seq_par = 0; e->seq_ping = 1;
            e->seq_step = (uint8_t)((p[TFM_P_SEQ_DIR] % 4) == 1 ? seq_len(e) - 1 : 0);
            seq_play_step(e, e->seq_step);
        } else {
            e->seq_t += beats;
            if (e->seq_t >= len) {
                e->seq_t -= len;
                if (e->seq_t > len) e->seq_t = 0.f;
                e->seq_par ^= 1u;
                e->seq_step = (uint8_t)seq_next(e, e->seq_step, 0);
                seq_play_step(e, e->seq_step);
            }
        }
        len = base * (e->seq_par ? 1.f - sw : 1.f + sw);
        float g = 0.05f + (float)p[TFM_P_SEQ_GATE] * (0.95f / 127.f);
        if (e->seq_open && g < 0.99f && e->seq_t >= g * len
            && e->seq.gate[seq_next(e, e->seq_step, 1)] != TFM_GATE_TIE) {
            release_list(e, e->seq_out, &e->seq_nout);
            e->seq_open = 0;
        }
    }
}

void tfm_set_step(tfm_engine *e, unsigned step, uint8_t note, uint8_t gate)
{
    if (step >= TFM_SEQ_STEPS) return;
    e->seq.note[step] = note > 127 ? 127 : note;
    e->seq.gate[step] = gate > 2 ? 0 : gate;
}
void tfm_load_seq(tfm_engine *e, const tfm_seq *s) { e->seq = *s; }
void tfm_transport(tfm_engine *e, int run)
{
    if (run && !e->seq_run) {
        e->seq_restart = 1; e->seq_tr = 0;
        for (unsigned k = 0; k < TFM_LFOS; k++) if (e->patch.p[TFM_P_LFO1_SYNC + k]) e->lfo_ph[k] = 0.f;   /* LFO a tempo: ripartono col sequencer */
    }
    if (!run && e->seq_run) { release_list(e, e->seq_out, &e->seq_nout); e->seq_open = 0; }
    e->seq_run = run ? 1 : 0;
}
unsigned tfm_play_state(const tfm_engine *e)
{
    return (unsigned)e->seq_step | (unsigned)e->seq_run << 8 | (unsigned)(e->patch.p[TFM_P_ARP_ON] && e->arp_n) << 9;
}

/* Avanza un ADSR di un blocco e restituisce il livello 0..1 (filtro e modulazione). */
static float adsr_step(tfm_adsr *a, uint8_t A, uint8_t D, uint8_t S, uint8_t R, unsigned n)
{
    float s = (float)S * (1.f / 127.f);
    switch (a->stage) {
    case 1: /* attack: punta a 1.2 per una curva "analogica" */
        a->level += (1.2f - a->level) * adsr_coef(A, n);
        if (a->level >= 1.f) { a->level = 1.f; a->stage = 2; }
        break;
    case 2:
        a->level += (s - a->level) * adsr_coef(D, n);
        break;
    case 3:
        a->level += (0.f - a->level) * adsr_coef(R, n);
        if (a->level < 0.0001f) { a->level = 0.f; a->stage = 0; }
        break;
    default:
        break;
    }
    return a->level;
}

/* ---------------- modulazioni ---------------- */

static inline float rnd_bipolar(tfm_engine *e)
{
    e->rnd = e->rnd * 1664525u + 1013904223u;
    return (float)(int32_t)e->rnd * (1.f / 2147483648.f);
}
/* seno in cicli (Bhaskara) */
static inline float sin_cyc(float t)
{
    t -= (float)(int)t;
    if (t < 0) t += 1.f;
    float x = t < 0.5f ? t : t - 0.5f, q = x * (1.f - 2.f * x), y = 32.f * q / (5.f - 8.f * q);
    return t < 0.5f ? y : -y;
}
/* forme: 0 TRI 1 SAW 2 RAMP 3 RECT 4 NOIZ 5 SINE; valore -1..1 */
static float lfo_shape(unsigned shape, float ph, float hold)
{
    switch (shape) {
    case 0: return 1.f - 4.f * (ph < 0.5f ? 0.5f - ph : ph - 0.5f);
    case 1: return 2.f * ph - 1.f;
    case 2: return 1.f - 2.f * ph;
    case 3: return ph < 0.5f ? 1.f : -1.f;
    case 4: return hold;
    default: return sin_cyc(ph);
    }
}
/* modi: 0 BIPO 1 UNI+ 2 UNI- 3 REC+ 4 REC- */
static float lfo_mode(unsigned mode, float s)
{
    switch (mode) {
    case 1: return 0.5f + 0.5f * s;
    case 2: return -(0.5f + 0.5f * s);
    case 3: return s < 0 ? -s : s;
    case 4: return s < 0 ? s : -s;
    default: return s;
    }
}
/* velocita': fader 0..127 -> 0.03 .. 20 Hz */
static inline float lfo_hz(uint8_t v) { return 0.03f * tfm_exp2f((float)v * (9.38f / 127.f)); }

/* accumulo delle modulazioni su una copia dei parametri */
typedef struct { uint8_t id[24]; float d[24]; unsigned n; float pitch; } mod_acc;
static inline void acc_add(mod_acc *m, unsigned target, int amt, float val)
{
    if (!target || target >= TFM_MOD_TARGETS || !amt) return;
    float x = (float)amt * (1.f / 20.f) * val;   /* -1..1 della corsa */
    unsigned id = mod_param[target];
    if (id == MOD_PITCH_ID) { m->pitch += x * 12.f; return; }   /* fino a +-12 semitoni */
    for (unsigned i = 0; i < m->n; i++) if (m->id[i] == id) { m->d[i] += x * 127.f; return; }
    if (m->n < 24) { m->id[m->n] = (uint8_t)id; m->d[m->n++] = x * 127.f; }
}
static inline void acc_apply(const mod_acc *m, uint8_t *eff)
{
    for (unsigned i = 0; i < m->n; i++) {
        float v = (float)eff[m->id[i]] + m->d[i];
        eff[m->id[i]] = (uint8_t)(v < 0.f ? 0.f : v > 127.f ? 127.f : v + 0.5f);
    }
}
static inline int link_amt(const uint8_t *p, unsigned id) { return (int)p[id + 1] - TFM_AMT_ZERO; }

/* contesto di un sotto-blocco per le voci: lo stesso per i due core */
typedef struct {
    const uint8_t *p, *eff;
    const float *mixc;
    float pitch, bend, kt, gl_c, rise_c, fall_c, fall_to, gl, gr;
    unsigned algo, algo2, n;
    int menv_on, modded;
    uint32_t ver;
    uint8_t ms20;
} voice_ctx;

/* Le voci first, first+step, ... di un sotto-blocco, sommate in out (n frame stereo). */
static void render_voices(tfm_engine *e, const voice_ctx *c, unsigned first, unsigned step, int32_t *out)
{
    const uint8_t *p = c->p, *eff = c->eff;
    const float *mixc = c->mixc;
    unsigned algo = c->algo, algo2 = c->algo2, n = c->n;
    int menv_on = c->menv_on;
    uint8_t ms20 = c->ms20;
    float kt = c->kt, bend = c->bend, gl_c = c->gl_c, rise_c = c->rise_c, fall_c = c->fall_c, fall_to = c->fall_to;
    float gl = c->gl, gr = c->gr;
    uint8_t veff[TFM_P_COUNT];      /* parametri di una voce (inviluppo di modulazione) */
    struct { float pitch; } g = { c->pitch };
for (unsigned vi = first; vi < TFM_VOICES; vi += step) {
        tfm_voice *v = &e->v[vi];
        if (v->note < 0) continue;
        if (!v->gate && voice_silent(v, algo, algo2)) { v->note = -1; continue; }
        const uint8_t *pv = eff;
        float pitch = g.pitch;
        if (menv_on) {   /* inviluppo di modulazione della voce */
            float m = adsr_step(&v->menv, p[TFM_P_MENV_A], p[TFM_P_MENV_D], p[TFM_P_MENV_S], p[TFM_P_MENV_R], n);
            mod_acc mv = { {0}, {0}, 0, 0.f };
            for (unsigned i = 0; i < TFM_LINKS; i++)
                acc_add(&mv, p[TFM_MENV_LINK(i)], link_amt(p, TFM_MENV_LINK(i)), m);
            tfm_memcpy(veff, eff, TFM_P_COUNT);
            acc_apply(&mv, veff);
            pv = veff;
            pitch += mv.pitch;
        }
        /* glide, rise, fall */
        v->pitch_cur += ((float)v->note - v->pitch_cur) * gl_c;
        v->rise_off -= v->rise_off * rise_c;
        if (!v->gate && p[TFM_P_FALL]) v->fall_off += (fall_to - v->fall_off) * fall_c;
        {   /* si ricalcola solo se qualcosa e' cambiato (pitch, parametri, modulazioni) */
            float pt = v->pitch_cur + v->detune + bend + pitch + v->rise_off + v->fall_off;
            float dp = pt - v->setup_pitch;
            if (c->modded || pv != eff || v->setup_ver != c->ver || dp > 0.0001f || dp < -0.0001f) {
                voice_setup_ops(v, 0, pv, pt);
                v->setup_pitch = pt;
                v->setup_ver = c->ver;
            }
        }

        /* filtro della voce: base + inviluppo + keytracking (con i parametri modulati) */
        flt_ctl fc;
        fc.ms20 = ms20;
        float res = (float)pv[TFM_P_RES] * (1.f / 127.f);
        fc.k = ms20 ? res * 5.6f : res * 4.05f;   /* MS20 va oltre l'autooscillazione */
        float heat = (float)pv[TFM_P_HEAT] * (1.f / 127.f);
        fc.drive = 0.35f + heat * heat * 6.f;
        fc.makeup = 1.f / (fc.drive < 1.f ? 0.35f + 0.65f * fc.drive : 1.f + 0.25f * (fc.drive - 1.f));
        fc.wobble = (float)pv[TFM_P_WOBBLE] * (2.f / 127.f);
        fc.mix = mixc;
        float lvl = adsr_step(&v->fenv, p[TFM_P_FLT_A], p[TFM_P_FLT_D], p[TFM_P_FLT_S], p[TFM_P_FLT_R], n);
        float oct = (float)pv[TFM_P_CUTOFF] * (10.f / 127.f) + (float)pv[TFM_P_FLT_ENV] * (8.f / 127.f) * lvl
                  + kt * ((float)v->note - 12.f) * (1.f / 12.f);
        float hz = 20.f * tfm_exp2f(oct);
        float fn = tfm_clampf(hz * (1.f / (float)TFM_SAMPLE_RATE), 0.0002f, 0.45f);
        float gg = tfm_prewarp(fn);
        float G1 = gg / (1.f + gg);
        float G = v->g_prev > 0.f ? v->g_prev : G1;
        float dG = (G1 - G) / (float)n;
        v->g_prev = G1;
        unsigned fb1 = f_to(pv[TFM_P_FB1], 8), fb2 = f_to(pv[TFM_P_FB2], 8);
        float vol = (float)pv[TFM_P_VOLUME] * (1.f / 127.f);
        float master = vol * vol * 8388607.f * 0.28f;
        /* voci dispari a sinistra, pari a destra (come OUT1/OUT2), con spread e tremolo */
        float pl = (1.f - v->pan) * v->gain * gl, pr = v->pan * v->gain * gr;
        for (unsigned s = 0; s < n; s++) {
            float x = (float)yac512(voice_fm(v, algo, fb1, algo2, fb2)) * (1.f / 4096.f);
            float y = flt_process(&v->flt, &fc, G, x) * master;
            G += dG;
            out[s * 2] += (int32_t)(y * pl);
            out[s * 2 + 1] += (int32_t)(y * pr);
        }
    }
}

/* secondo core: le voci dispari nel suo buffer */
static tfm_engine *par_e;
static voice_ctx par_ctx;
static int32_t par_out[2 * 32];
void tfm_parallel_job(uint32_t arg)
{
    (void)arg;
    tfm_memset(par_out, 0, sizeof par_out);
    render_voices(par_e, &par_ctx, 1, 2, par_out);
}
void tfm_set_parallel(tfm_engine *e, int (*begin)(void), int (*end)(void))
{
    par_e = e;
    e->par_begin = begin;
    e->par_end = end;
}

#define TFM_SUB 32u /* sotto-blocco per i controlli */

void tfm_render(tfm_engine *e, int32_t *output, unsigned frames)
{
    const uint8_t *p = e->patch.p;
    unsigned algo = algo_of(e);
    unsigned algo2 = p[TFM_P_ALGO2] ? 1u : 0u;
    const float *mixc = flt_mix[flt_mode_of(e)];
    uint8_t ms20 = p[TFM_P_FLAVOUR] ? 1 : 0;
    float kt = (float)(p[TFM_P_KEYTRACK] > 10 ? 10 : p[TFM_P_KEYTRACK]) * 0.1f;
    float bend = bend_semis(e);
    int menv_on = 0;
    for (unsigned i = 0; i < TFM_LINKS; i++)
        if (p[TFM_MENV_LINK(i)] && link_amt(p, TFM_MENV_LINK(i))) menv_on = 1;
    /* tempi del glide e di rise/fall, per sotto-blocco */
    float gl_t = p[TFM_P_PORTA] && p[TFM_P_GLIDE] ? 0.005f * tfm_exp2f((float)p[TFM_P_GLIDE] * (9.f / 127.f)) : 0.f;   /* fino a ~2.6 s */
    float sub = (float)TFM_SUB / (float)TFM_SAMPLE_RATE;
    float gl_c = gl_t > 0.f ? 1.f - tfm_exp2f(-3.f * 1.4427f * sub / gl_t) : 1.f;
    float rise_c = 1.f - tfm_exp2f(-1.4427f * sub / 0.05f);   /* ~150 ms per il 95% */
    float fall_t = 0.15f + (float)p[TFM_P_FALL] * (0.6f / 127.f);
    float fall_c = 1.f - tfm_exp2f(-1.4427f * sub / fall_t);
    float fall_to = -(float)p[TFM_P_FALL] * (24.f / 127.f);

    uint8_t eff[TFM_P_COUNT];       /* parametri con le modulazioni globali (LFO) */
    float gl = 1.f, gr = 1.f;       /* tremolo */

    tfm_memset(output, 0, frames * 2 * sizeof(int32_t));
    for (unsigned start = 0; start < frames; start += TFM_SUB) {
        unsigned n = frames - start < TFM_SUB ? frames - start : TFM_SUB;
        float dt = (float)n / (float)TFM_SAMPLE_RATE;
        int32_t *out = output + start * 2;
        player_tick(e, n);   /* arpeggiatore e sequencer */
        /* --- LFO, vibrato, tremolo (globali) --- */
        mod_acc g = { {0}, {0}, 0, 0.f };
        for (unsigned k = 0; k < TFM_LFOS; k++) {
            const uint8_t *q = p + TFM_LFO_BASE(k);
            int used = 0;
            for (unsigned i = 0; i < TFM_LINKS; i++)
                if (p[TFM_LFO_LINK(k, i)] && link_amt(p, TFM_LFO_LINK(k, i))) used = 1;
            float hz = lfo_hz(q[0]);
            if (p[TFM_P_LFO1_SYNC + k])   /* a tempo: una divisione della nota da 1/4 */
                hz = (float)tfm_bpm_now(e) * (1.f / 60.f) / tfm_div_beats[p[TFM_P_LFO1_DIV + k] % TFM_DIVS];
            e->lfo_ph[k] += hz * dt;
            if (e->lfo_ph[k] >= 1.f) { e->lfo_ph[k] -= (float)(int)e->lfo_ph[k]; e->lfo_hold[k] = rnd_bipolar(e); }
            if (!used) continue;
            float val = lfo_mode(q[2] % TFM_LFO_MODES, lfo_shape(q[1] % TFM_LFO_SHAPES, e->lfo_ph[k], e->lfo_hold[k]));
            for (unsigned i = 0; i < TFM_LINKS; i++)
                acc_add(&g, p[TFM_LFO_LINK(k, i)], link_amt(p, TFM_LFO_LINK(k, i)), val);
        }
        {   /* vibrato: profondita' non lineare, fino a 2 semitoni */
            float vr = 0.5f * tfm_exp2f((float)p[TFM_P_VIB_RATE] * (4.6f / 127.f));
            e->vib_ph += vr * dt;
            if (e->vib_ph >= 1.f) { e->vib_ph -= (float)(int)e->vib_ph; e->vib_hold = rnd_bipolar(e); }
            if (p[TFM_P_VIB_DEPTH]) {
                static const uint8_t vshape[5] = { 5, 3, 1, 2, 4 };   /* SINE RECT SAW RAMP NOIZ */
                float d = (float)p[TFM_P_VIB_DEPTH] * (1.f / 127.f);
                float s = lfo_shape(vshape[p[TFM_P_VIB_WAVE] % 5], e->vib_ph, e->vib_hold);
                unsigned vm = p[TFM_P_VIB_MODE] % 3;
                if (vm == 1) s = 0.5f + 0.5f * s;
                else if (vm == 2) s = -(0.5f + 0.5f * s);
                g.pitch += d * d * 2.f * s;
            }
        }
        if (p[TFM_P_TREM_DEPTH]) {   /* tremolo stereo con sfasamento L/R */
            float tr = 0.5f * tfm_exp2f((float)p[TFM_P_TREM_RATE] * (4.6f / 127.f));
            float d = (float)p[TFM_P_TREM_DEPTH] * (1.f / 127.f);
            float off = (float)p[TFM_P_TREM_PHASE] * (0.5f / 127.f);
            e->trem_ph += tr * dt;
            if (e->trem_ph >= 1.f) e->trem_ph -= (float)(int)e->trem_ph;
            gl = 1.f - d * (0.5f + 0.5f * sin_cyc(e->trem_ph));
            gr = 1.f - d * (0.5f + 0.5f * sin_cyc(e->trem_ph + off));
        } else {
            gl = gr = 1.f;
        }
        tfm_memcpy(eff, p, TFM_P_COUNT);
        acc_apply(&g, eff);

        {   /* le voci: con due core, pari qui e dispari sul secondo */
            voice_ctx c = { p, eff, mixc, g.pitch, bend, kt, gl_c, rise_c, fall_c, fall_to, gl, gr, algo, algo2, n, menv_on, g.n > 0, e->ver, ms20 };
            if (e->par_begin && n <= 32) {
                par_ctx = c;
                int on2 = e->par_begin();
                render_voices(e, &c, 0, 2, out);
                if (!on2 || e->par_end() != 0) tfm_parallel_job(0);   /* non l'ha preso: lo si fa qui */
                for (unsigned s = 0; s < 2 * n; s++) out[s] += par_out[s];
            } else {
                render_voices(e, &c, 0, 1, out);
            }
        }
    }
    if (e->ext_sync && e->ext_bpm > 60.f) {   /* l'eco a tempo segue il clock esterno */
        float b = e->ext_bpm - 60.f;
        eff[TFM_P_BPM] = (uint8_t)(b > 127.f ? 127.f : b);
    }
    p = eff;   /* gli effetti con le modulazioni dell'ultimo sotto-blocco */
    /* effetti, blocco della componente continua (~10 Hz) e limitatore morbido in Q23 */
    float buf[2 * TFM_SUB];
    for (unsigned start = 0; start < frames; start += TFM_SUB) {
        unsigned n = frames - start < TFM_SUB ? frames - start : TFM_SUB;
        int32_t *o = output + start * 2;
        for (unsigned i = 0; i < n * 2; i++) buf[i] = (float)o[i] * (1.f / 8388607.f);
        tfm_fx_process(&e->fx, p, buf, n);
        for (unsigned i = 0; i < n * 2; i++) {
            unsigned ch = i & 1;
            float x = buf[i];
            float y = x - e->dc_x[ch] + 0.99857f * e->dc_y[ch];
            e->dc_x[ch] = x;
            e->dc_y[ch] = y;
            e->og += (e->og_target - e->og) * 0.002f;   /* MASTER senza scatti */
            o[i] = (int32_t)(tfm_tanhf(y * 2.5f * e->og) * 8388607.f);   /* +8 dB e MASTER prima del limitatore morbido (tanh) */
        }
    }
}
