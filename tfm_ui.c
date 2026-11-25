/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Interfaccia "stile TWISTfm" per l'FM-1. Il font 5x7 e la palette vengono
 * da app/screen.c di FM1 Open (Copyright (c) 2026 M-VAVE, stessa licenza).
 *
 * Disegno a bande di 16 righe come il firmware originale: ogni banda viene
 * ricomposta in un buffer da 7,5 KB e trasmessa solo se il suo contenuto e'
 * cambiato (hash). Niente animazioni: lo schermo lavora solo dopo un comando,
 * cosi' la CPU resta all'audio. */
#include "tfm_ui.h"
#include "tfm_presets.h"
#include "tfm_fx.h"
#include "tfm_util.h"

enum { BACK = 0x0000, PANEL = 0x18e3, TEXT = 0xef5d, ACCENT = 0x07ff, DIM = 0x738e, GREEN = 0x07e0, AMBER = 0xfd20 };
static const uint16_t knob_color[4] = { 0x3c9f, 0x07bf, 0x4eae, 0x06d4 };

static const uint8_t font[36][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
static const uint8_t punct[][6] = {
    {45,8,8,8,8,8},{47,0x60,0x18,6,1,0},{43,8,8,0x3e,8,8},{35,0x14,0x7f,0x14,0x7f,0x14},{37,0x63,0x13,8,0x64,0x63},
    {62,0x41,0x22,0x14,8,0},{63,2,1,0x51,9,6},{33,0,0,0x5f,0,0},{40,0,0x1c,0x22,0x41,0},{41,0,0x41,0x22,0x1c,0},
    {42,0x14,8,0x3e,8,0x14},{44,0,0x50,0x30,0,0},{46,0,0x60,0x60,0,0},{58,0,0x36,0x36,0,0},{60,8,0x14,0x22,0x41,0},
    {61,0x14,0x14,0x14,0x14,0x14},{124,0,0,0x7f,0,0}};

/* ------------------------------------------------------------------ */
/* Sezioni, pagine e parametri                                         */
/* ------------------------------------------------------------------ */

enum { S_HOME, S_EDIT, S_ENV, S_FILTER, S_GLO, S_INFO, S_FX, S_LFO, S_COUNT };
enum { T_F, T_WAVE, T_MULT, T_LIST, T_TUNE, T_NONE, T_VOICES, T_MS, T_AMT };

typedef struct { uint8_t type; uint16_t id; const char *label; const char *const *list; uint8_t count; } pdesc;

static const char *const L_WAVE[8] = { "SINE", "HALF", "ABS", "PULS", "EVEN", "EABS", "SQUA", "DSQR" };
static const char *const L_MULT[16] = { ".5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "10", "12", "12", "15", "15" };
static const char *const L_ALGO[TFM_ALGOS] = { "1>2>3>4", "1+2>3>4", "1>2+3>4", "1+2>3+4", "1+2+3+4",
                                              "(1+2)>3>4", "1>(2+3+4)", "(1+2+3)>4", "1>2>(3+4)", "1>2+3+4" };
static const char *const L_FT[15] = { "LP4", "LP3", "LP2", "LP1", "BP4", "BP2", "NOTC", "PHAS", "HP1", "HP2", "HP3", "H2+L", "H3+L", "NO+L", "PH+L" };
static const char *const L_FLAV[2] = { "LIQD", "MS20" };
static const char *const L_L2[2] = { "SER", "PAR" };
static const char *const L_KSL[4] = { "0", "-1.5", "-3", "-6" };
static const char *const L_OFFON[2] = { "OFF", "ON" };
static const char *const L_MODE[TFM_VOICE_MODES] = { "POL8", "UNIS", "4X2", "2X4", "MONO", "WIDE", "RRBN" };
static const char *const L_MODE_LONG[TFM_VOICE_MODES] = { "POLIFONIA", "UNISONO", "4 NOTE X 2", "2 NOTE X 4", "MONO L+R", "WIDE PINGPONG", "ROUND ROBIN" };
static const char *const L_VOICES[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
static const char *const L_PRIO[3] = { "LAST", "HIGH", "LOW" };
static const char *const L_DIST[TFM_DIST_SHAPES] = { "TUBE", "FUZZ", "DIOD", "FOLD", "RECT", "CRSH", "HARD", "SINE", "SPLT", "FDBK" };
static const char *const L_DIST_LONG[TFM_DIST_SHAPES] = { "TUBE", "FUZZ", "DIODO", "WAVEFOLD", "RETTIFICA", "BITCRUSH", "HARD CLIP", "SINE FOLD", "SPLIT", "FEEDBACK" };
static const char *const L_REV[TFM_REV_TYPES] = { "ROOM", "PLAT", "SHIM" };
static const char *const L_REV_LONG[TFM_REV_TYPES] = { "ROOM", "PLATE", "LONG SHIMMER" };
static const uint8_t fx_mix_id[4] = { TFM_P_DIST_MIX, TFM_P_PH_MIX, TFM_P_DLY_MIX, TFM_P_REV_MIX };
static const char *const L_SLOT[5] = { "--", "DIST", "PHAS", "TAPE", "REV" };
static const char *const L_LSHAPE[TFM_LFO_SHAPES] = { "TRI", "SAW", "RAMP", "RECT", "NOIZ", "SINE" };
static const char *const L_LMODE[TFM_LFO_MODES] = { "BIPO", "UNI+", "UNI-", "REC+", "REC-" };
static const char *const L_VWAVE[5] = { "SINE", "RECT", "SAW", "RAMP", "NOIZ" };
static const char *const L_VMODE[3] = { "BIPO", "UP", "DOWN" };
static const char *const L_GLMODE[2] = { "ALL", "LEGT" };
static const char *const L_TEN[11] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };

#define OPID(f) (0x100u | (f)) /* parametro dell'operatore selezionato */
#define FB_ID 0x200u            /* feedback dell'operatore selezionato */
static const pdesc P_HOME[4] = { {T_F, TFM_P_CUTOFF, "CUTOFF", 0, 0}, {T_F, TFM_P_RES, "RES", 0, 0}, {T_F, TFM_P_FLT_ENV, "ENV", 0, 0}, {T_F, TFM_P_HEAT, "HEAT", 0, 0} };
static const pdesc P_EDIT[2][4] = {
    { {T_F, OPID(TFM_OPP_VOL), "VOL", 0, 0}, {T_WAVE, OPID(TFM_OPP_WAVE), "WAVE", L_WAVE, 8}, {T_MULT, OPID(TFM_OPP_MULT), "MULT", L_MULT, 16}, {T_LIST, OPID(TFM_OPP_KVS), "VELO", L_TEN, 11} },
    { {T_LIST, OPID(TFM_OPP_KSL), "KSL", L_KSL, 4}, {T_LIST, OPID(TFM_OPP_KSR), "KSR", L_OFFON, 2}, {T_F, FB_ID, "FB", 0, 0}, {T_NONE, 0, "", 0, 0} } };
static const pdesc P_ENV[4] = { {T_F, OPID(TFM_OPP_A), "ATK", 0, 0}, {T_F, OPID(TFM_OPP_D), "DEC", 0, 0}, {T_F, OPID(TFM_OPP_S), "SUS", 0, 0}, {T_F, OPID(TFM_OPP_R), "REL", 0, 0} };
static const pdesc P_FILTER[3][4] = {
    { {T_LIST, TFM_P_FLT_TYPE, "TYPE", L_FT, 15}, {T_F, TFM_P_CUTOFF, "CUTOFF", 0, 0}, {T_F, TFM_P_RES, "RES", 0, 0}, {T_F, TFM_P_FLT_ENV, "ENV", 0, 0} },
    { {T_F, TFM_P_HEAT, "HEAT", 0, 0}, {T_LIST, TFM_P_FLAVOUR, "FLAV", L_FLAV, 2}, {T_F, TFM_P_WOBBLE, "WOBBLE", 0, 0}, {T_LIST, TFM_P_KEYTRACK, "KTRACK", L_TEN, 11} },
    { {T_F, TFM_P_FLT_A, "ATK", 0, 0}, {T_F, TFM_P_FLT_D, "DEC", 0, 0}, {T_F, TFM_P_FLT_S, "SUS", 0, 0}, {T_F, TFM_P_FLT_R, "REL", 0, 0} } };
static const pdesc P_GLO[6][4] = {
    { {T_LIST, TFM_P_VOICE_MODE, "MODE", L_MODE, TFM_VOICE_MODES}, {T_VOICES, TFM_P_VOICES, "VOICES", L_VOICES, 8}, {T_LIST, TFM_P_PRIO, "PRIO", L_PRIO, 3}, {T_F, TFM_P_FAT, "FAT", 0, 0} },
    { {T_F, TFM_P_FB1, "FB 1", 0, 0}, {T_LIST, TFM_P_ALGO2, "LAYER2", L_L2, 2}, {T_F, TFM_P_FB2, "FB 5", 0, 0}, {T_TUNE, TFM_P_L2_TUNE, "L2TUNE", 0, 49} },
    { {T_F, TFM_P_VOLUME, "VOLUME", 0, 0}, {T_NONE, 0, "", 0, 0}, {T_NONE, 0, "", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_F, TFM_P_VIB_RATE, "RATE", 0, 0}, {T_F, TFM_P_VIB_DEPTH, "DEPTH", 0, 0}, {T_LIST, TFM_P_VIB_WAVE, "WAVE", L_VWAVE, 5}, {T_LIST, TFM_P_VIB_MODE, "MODE", L_VMODE, 3} },
    { {T_F, TFM_P_TREM_RATE, "RATE", 0, 0}, {T_F, TFM_P_TREM_DEPTH, "DEPTH", 0, 0}, {T_F, TFM_P_TREM_PHASE, "PHASE", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_F, TFM_P_GLIDE, "GLIDE", 0, 0}, {T_LIST, TFM_P_GLIDE_MODE, "GL MOD", L_GLMODE, 2}, {T_F, TFM_P_RISE, "RISE", 0, 0}, {T_F, TFM_P_FALL, "FALL", 0, 0} } };
#define GLO_PAGES 6
static const pdesc P_FX[7][4] = {
    { {T_LIST, TFM_P_DIST_TYPE, "TYPE", L_DIST, TFM_DIST_SHAPES}, {T_F, TFM_P_DIST_DRIVE, "DRIVE", 0, 0}, {T_F, TFM_P_DIST_TEMP, "TEMP", 0, 0}, {T_F, TFM_P_DIST_MIX, "MIX", 0, 0} },
    { {T_F, TFM_P_PH_RATE, "RATE", 0, 0}, {T_F, TFM_P_PH_DEPTH, "DEPTH", 0, 0}, {T_F, TFM_P_PH_FB, "FDBK", 0, 0}, {T_F, TFM_P_PH_MIX, "MIX", 0, 0} },
    { {T_MS, TFM_P_DLY_TIME, "TIME MS", 0, 0}, {T_F, TFM_P_DLY_FB, "FDBK", 0, 0}, {T_F, TFM_P_DLY_TONE, "TONE", 0, 0}, {T_F, TFM_P_DLY_MIX, "MIX", 0, 0} },
    { {T_F, TFM_P_DLY_WOW, "WOW", 0, 0}, {T_F, TFM_P_DLY_SAT, "SAT", 0, 0}, {T_F, TFM_P_DLY_SPREAD, "SPREAD", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_LIST, TFM_P_REV_TYPE, "TYPE", L_REV, TFM_REV_TYPES}, {T_F, TFM_P_REV_DECAY, "DECAY", 0, 0}, {T_F, TFM_P_REV_TONE, "TONE", 0, 0}, {T_F, TFM_P_REV_MIX, "MIX", 0, 0} },
    { {T_F, TFM_P_REV_PREDLY, "PREDLY", 0, 0}, {T_F, TFM_P_REV_MOD, "MOD", 0, 0}, {T_F, TFM_P_REV_SHIM, "SHIMMR", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_LIST, TFM_P_FX_SLOT1, "SLOT 1", L_SLOT, 5}, {T_LIST, TFM_P_FX_SLOT2, "SLOT 2", L_SLOT, 5}, {T_LIST, TFM_P_FX_SLOT3, "SLOT 3", L_SLOT, 5}, {T_LIST, TFM_P_FX_SLOT4, "SLOT 4", L_SLOT, 5} } };
/* LFO: per ognuno dei 3 LFO una pagina principale e due di chain (4 collegamenti), poi l'inviluppo di modulazione */
#define LMAIN(k) { {T_F, TFM_LFO_BASE(k), "RATE", 0, 0}, {T_LIST, TFM_LFO_BASE(k) + 1, "SHAPE", L_LSHAPE, TFM_LFO_SHAPES}, \
                   {T_LIST, TFM_LFO_BASE(k) + 2, "MODE", L_LMODE, TFM_LFO_MODES}, {T_LIST, TFM_LFO_BASE(k) + 3, "RETRIG", L_OFFON, 2} }
#define LCH(base, pg, la, lb) { {T_LIST, (base) + 4 * (pg), "DEST " la, tfm_mod_target_names, TFM_MOD_TARGETS}, {T_AMT, (base) + 4 * (pg) + 1, "AMT " la, 0, 41}, \
                              {T_LIST, (base) + 4 * (pg) + 2, "DEST " lb, tfm_mod_target_names, TFM_MOD_TARGETS}, {T_AMT, (base) + 4 * (pg) + 3, "AMT " lb, 0, 41} }
static const pdesc P_LFO[12][4] = {
    LMAIN(0), LCH(TFM_P_LFO1_LINKS, 0, "1", "2"), LCH(TFM_P_LFO1_LINKS, 1, "3", "4"),
    LMAIN(1), LCH(TFM_P_LFO2_LINKS, 0, "1", "2"), LCH(TFM_P_LFO2_LINKS, 1, "3", "4"),
    LMAIN(2), LCH(TFM_P_LFO3_LINKS, 0, "1", "2"), LCH(TFM_P_LFO3_LINKS, 1, "3", "4"),
    { {T_F, TFM_P_MENV_A, "ATK", 0, 0}, {T_F, TFM_P_MENV_D, "DEC", 0, 0}, {T_F, TFM_P_MENV_S, "SUS", 0, 0}, {T_F, TFM_P_MENV_R, "REL", 0, 0} },
    LCH(TFM_P_MENV_LINKS, 0, "1", "2"), LCH(TFM_P_MENV_LINKS, 1, "3", "4") };
static const uint8_t page_count[S_COUNT] = { 1, 2, 1, 3, GLO_PAGES, 1, 7, 12 };

static const uint8_t black_keys[11] = { 1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25 };

struct tfm_ui {
    tfm_patch patch;
    tfm_event q[64];
    uint8_t qh, qt;
    uint8_t section, page[S_COUNT], op, preset, info;
    int8_t octave;
    int8_t touched;           /* ultimo knob mosso, -1 nessuno */
    uint8_t key_note[TFM_KEYS];
    uint8_t held_btn[TFM_BTN_COUNT];
    uint8_t dirty;
    uint32_t band_hash[TFM_SCREEN_H / 16];
    /* due buffer di banda da 240 x 16 pixel: mentre il DMA invia uno, si disegna l'altro */
    uint16_t bands[2][TFM_SCREEN_W * 16];
    uint16_t *band;
    unsigned band_top, which;
    tfm_blit_fn blit;
#ifndef TFM_UI_NO_FRAMEBUFFER
    uint16_t fb[TFM_SCREEN_W * TFM_SCREEN_H];
#endif
};

unsigned tfm_ui_size(void) { return (sizeof(tfm_ui) + 7u) & ~7u; }
void tfm_ui_set_blit(tfm_ui *u, tfm_blit_fn fn) { u->blit = fn; u->dirty = 1; tfm_memset(u->band_hash, 0xff, sizeof u->band_hash); }
unsigned tfm_ui_preset_count(void) { return TFM_PRESET_COUNT; }
const char *tfm_ui_preset_name(unsigned i) { return i < TFM_PRESET_COUNT ? tfm_presets[i].name : ""; }
const tfm_patch *tfm_ui_patch(const tfm_ui *u) { return &u->patch; }
#ifndef TFM_UI_NO_FRAMEBUFFER
const uint16_t *tfm_ui_framebuffer(const tfm_ui *u) { return u->fb; }
#endif

static void push(tfm_ui *u, uint8_t type, uint8_t a, uint8_t b)
{
    uint8_t next = (uint8_t)((u->qt + 1) & 63);
    if (next == u->qh) return; /* coda piena: l'evento si perde, mai bloccare */
    u->q[u->qt].type = type; u->q[u->qt].a = a; u->q[u->qt].b = b;
    u->qt = next;
}
int tfm_ui_next_event(tfm_ui *u, tfm_event *out)
{
    if (u->qh == u->qt) return 0;
    *out = u->q[u->qh];
    u->qh = (uint8_t)((u->qh + 1) & 63);
    return 1;
}

static void load_preset(tfm_ui *u, unsigned i)
{
    for (unsigned k = 0; k < TFM_P_COUNT; k++) u->patch.p[k] = tfm_presets[i].p[k];
    u->preset = (uint8_t)i;
    push(u, TFM_EV_PATCH, (uint8_t)i, 0);
    u->dirty = 1;
}

tfm_ui *tfm_ui_init(void *memory)
{
    tfm_ui *u = (tfm_ui *)memory;
    tfm_memset(u, 0, sizeof *u);
    tfm_memset(u->key_note, 0xff, sizeof u->key_note);
    tfm_memset(u->band_hash, 0xff, sizeof u->band_hash);
    u->band = u->bands[0];
    u->touched = -1;
    u->qh = u->qt = 0;
    load_preset(u, TFM_PRESET_COUNT > 1 ? 1 : 0);
    u->section = S_HOME;
    u->dirty = 1;
    return u;
}

/* ---- parametri ---- */
static void fx_slots(const tfm_ui *u, uint8_t out[4]);
static unsigned algo_cur(const tfm_ui *u) { unsigned a = u->patch.p[TFM_P_ALGO]; return a < TFM_ALGOS ? a : TFM_ALGOS - 1; }
static unsigned mode_cur(const tfm_ui *u) { unsigned m = u->patch.p[TFM_P_VOICE_MODE]; return m < TFM_VOICE_MODES ? m : 0; }
static int mode_is_poly(const tfm_ui *u) { unsigned m = mode_cur(u); return m == 0 || m == 5 || m == 6; }
static unsigned voices_cur(const tfm_ui *u) { unsigned v = u->patch.p[TFM_P_VOICES]; return v < 1 || v > 8 ? 8 : v; }
static int mode_is_mono(const tfm_ui *u) { unsigned m = mode_cur(u); return m == 1 || m == 4 || (mode_is_poly(u) && voices_cur(u) == 1); }
static int resolve_id(const tfm_ui *u, const pdesc *d)
{
    if (d->type == T_NONE) return -1;
    if (d->id == FB_ID) /* feedback: solo OP1 e OP5 */
        return u->op == 0 ? TFM_P_FB1 : u->op == 4 ? TFM_P_FB2 : -1;
    if (d->id & 0x100) return (int)TFM_OP_BASE(u->op) + (d->id & 0xff);
    if (d->id == TFM_P_VOICES && !mode_is_poly(u)) return -1;  /* conta solo nei modi polifonici */
    if (d->id == TFM_P_PRIO && !mode_is_mono(u)) return -1;    /* conta solo quando si suona mono */
    if (d->id == TFM_P_REV_SHIM && u->patch.p[TFM_P_REV_TYPE] != 2) return -1; /* solo LONG SHIMMER */
    return d->id;
}
static unsigned list_index(const pdesc *d, uint8_t v)
{
    switch (d->type) {
    case T_WAVE: return v >> 4 > 7 ? 7 : v >> 4;
    case T_MULT: return v >> 3 > 15 ? 15 : v >> 3;
    case T_TUNE: return v < 40 ? 0 : v > 88 ? 48 : v - 40u;
    case T_VOICES: return v < 1 || v > 8 ? 7 : v - 1u;
    default: return v >= d->count ? d->count - 1u : v;
    }
}
static uint8_t list_store(const pdesc *d, unsigned i)
{
    switch (d->type) {
    case T_WAVE: return (uint8_t)(i * 16 + 8);
    case T_MULT: return (uint8_t)(i * 8 + 4);
    case T_TUNE: return (uint8_t)(i + 40);
    case T_VOICES: return (uint8_t)(i + 1);
    default: return (uint8_t)i;
    }
}
static const pdesc *page_params(const tfm_ui *u)
{
    switch (u->section) {
    case S_HOME: return P_HOME;
    case S_EDIT: return P_EDIT[u->page[S_EDIT]];
    case S_ENV: return P_ENV;
    case S_FILTER: return P_FILTER[u->page[S_FILTER]];
    case S_GLO: return P_GLO[u->page[S_GLO]];
    case S_FX: return P_FX[u->page[S_FX]];
    case S_LFO: return P_LFO[u->page[S_LFO]];
    default: return 0;
    }
}
static void set_param(tfm_ui *u, int id, int v)
{
    if (id < 0 || id >= TFM_P_COUNT) return;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    if (u->patch.p[id] == v) return;
    u->patch.p[id] = (uint8_t)v;
    push(u, TFM_EV_PARAM, (uint8_t)id, (uint8_t)v);
    u->dirty = 1;
}
static void turn_param(tfm_ui *u, const pdesc *d, int delta)
{
    int id = resolve_id(u, d);
    if (id < 0) return;
    if (d->type == T_F || d->type == T_MS) { set_param(u, id, u->patch.p[id] + delta); return; }
    int i = (int)list_index(d, u->patch.p[id]) + (delta > 0 ? 1 : -1);
    int n = d->type == T_TUNE ? 49 : d->count;
    if (d->type == T_AMT) { i = (int)list_index(d, u->patch.p[id]) + delta; }
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    if (id >= TFM_P_FX_SLOT1 && id <= TFM_P_FX_SLOT4 && i > 0) {
        /* se l'effetto e' gia' in un altro slot, i due slot si scambiano */
        for (int s = TFM_P_FX_SLOT1; s <= TFM_P_FX_SLOT4; s++)
            if (s != id && u->patch.p[s] == i) set_param(u, s, u->patch.p[id]);
    }
    set_param(u, id, list_store(d, (unsigned)i));
}
static void value_text(const tfm_ui *u, const pdesc *d, char *out)
{
    int id = resolve_id(u, d);
    if (id < 0) { tfm_strcpy(out, d->type == T_NONE ? "" : "-"); return; }
    uint8_t v = u->patch.p[id];
    if (d->type == T_F || d->type == T_MS) {
        unsigned n = v;
        if (d->type == T_MS) { /* 30 * 40^(v/127) ms, come nel motore */
            float ms = 30.f; for (unsigned k = 0; k < v; k++) ms *= 1.02511f;
            n = (unsigned)(ms + 0.5f);
        }
        char tmp[4]; int k = 0;
        do { tmp[k++] = (char)('0' + n % 10); n /= 10; } while (n);
        for (int i = 0; i < k; i++) out[i] = tmp[k - 1 - i];
        out[k] = 0;
        return;
    }
    if (d->type == T_TUNE || d->type == T_AMT) {
        int s = d->type == T_AMT ? (int)list_index(d, v) - TFM_AMT_ZERO : (int)list_index(d, v) - 24;
        char *p = out;
        if (s > 0) *p++ = '+';
        if (s < 0) { *p++ = '-'; s = -s; }
        if (s >= 10) *p++ = (char)('0' + s / 10);
        *p++ = (char)('0' + s % 10);
        *p = 0;
        return;
    }
    tfm_strcpy(out, d->list[list_index(d, v)]);
}

/* ---- comandi ---- */
static void goto_section(tfm_ui *u, unsigned s, unsigned info)
{
    if (u->section == s && s != S_INFO) u->page[s] = (uint8_t)((u->page[s] + 1) % page_count[s]);
    u->section = (uint8_t)s;
    u->info = (uint8_t)info;
    u->touched = -1;
    u->dirty = 1;
}

void tfm_ui_encoder(tfm_ui *u, unsigned enc, int delta)
{
    if (!delta) return;
    if (enc == TFM_ENC_PRESETS) {
        int p = (int)u->preset + (delta > 0 ? 1 : -1);
        if (p < 0) p = TFM_PRESET_COUNT - 1;
        if (p >= TFM_PRESET_COUNT) p = 0;
        load_preset(u, (unsigned)p);
        return;
    }
    if (enc == TFM_ENC_ALGORITHM) {
        int a = (int)algo_cur(u) + (delta > 0 ? 1 : -1);
        set_param(u, TFM_P_ALGO, a < 0 ? 0 : a >= TFM_ALGOS ? TFM_ALGOS - 1 : a);
        return;
    }
    if (enc == TFM_ENC_SELECT) {
        /* SELECT sfoglia le pagine; in ENV, che ha una pagina, sceglie l'operatore */
        if (u->section == S_ENV) u->op = (uint8_t)((u->op + (delta > 0 ? 1 : 5)) % 6);
        else if (page_count[u->section] > 1) {
            unsigned n = page_count[u->section];
            u->page[u->section] = (uint8_t)((u->page[u->section] + (delta > 0 ? 1 : n - 1)) % n);
        }
        u->touched = -1;
        u->dirty = 1;
        return;
    }
    if (enc >= TFM_ENC_K1 && enc <= TFM_ENC_K4) {
        const pdesc *ps = page_params(u);
        if (!ps) return;
        unsigned k = enc - TFM_ENC_K1;
        turn_param(u, &ps[k], delta);
        if (u->touched != (int8_t)k) { u->touched = (int8_t)k; u->dirty = 1; }
    }
}

void tfm_ui_button(tfm_ui *u, unsigned b, int down)
{
    if (b >= TFM_BTN_COUNT) return;
    u->held_btn[b] = (uint8_t)(down != 0);
    if (!down) return;
    switch (b) {
    case TFM_BTN_OCT_DOWN: if (u->octave > -3) u->octave--; u->dirty = 1; break;
    case TFM_BTN_OCT_UP: if (u->octave < 3) u->octave++; u->dirty = 1; break;
    case TFM_BTN_HOME: goto_section(u, S_HOME, 0); break;
    case TFM_BTN_EDIT: goto_section(u, S_EDIT, 0); break;
    case TFM_BTN_ENV: goto_section(u, S_ENV, 0); break;
    case TFM_BTN_FX: goto_section(u, S_FX, 0); break;
    case TFM_BTN_SEL: goto_section(u, S_FILTER, 0); break;
    case TFM_BTN_GLO: goto_section(u, S_GLO, 0); break;
    case TFM_BTN_LFO: /* LFO 1 > LFO 2 > LFO 3 > MOD ENV; le pagine di chain con SELECT */
        if (u->section == S_LFO) u->page[S_LFO] = (uint8_t)((u->page[S_LFO] / 3 + 1) % 4 * 3);
        u->section = S_LFO; u->touched = -1; u->dirty = 1;
        break;
    default: goto_section(u, S_INFO, b); break; /* SAVE, ARP, SEQ, PLAY, REC: fase 5 */
    }
}

void tfm_ui_key(tfm_ui *u, unsigned key, int down) { tfm_ui_key_vel(u, key, down, 100); }
void tfm_ui_key_vel(tfm_ui *u, unsigned key, int down, unsigned vel)
{
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    if (key >= TFM_KEYS) return;
    if (down) {
        /* in EDIT/ENV i tasti neri serigrafati scelgono l'operatore (come nel firmware M-VAVE) */
        if (u->section == S_EDIT || u->section == S_ENV) {
            for (unsigned i = 0; i < 10; i++)
                if (black_keys[i] == key) {
                    if (i < 6) u->op = (uint8_t)i;
                    else if (i < 8) { u->section = S_GLO; u->page[S_GLO] = i == 6 ? 5 : 0; }   /* PITCH, GLOBAL */
                    else set_param(u, TFM_P_VOICE_MODE, i == 8 ? 4 : 0);     /* MONO, POLY = POL8 */
                    u->touched = -1;
                    u->dirty = 1;
                    return;
                }
        }
        int note = 53 + (int)key + 12 * u->octave; /* il tasto 0 e' FA3 */
        if (note < 0 || note > 127) return;
        u->key_note[key] = (uint8_t)note;
        push(u, TFM_EV_NOTE_ON, (uint8_t)note, (uint8_t)vel);
    } else if (u->key_note[key] != 0xff) {
        push(u, TFM_EV_NOTE_OFF, u->key_note[key], 0);
        u->key_note[key] = 0xff;
    }
}

uint64_t tfm_ui_leds(const tfm_ui *u)
{
    uint64_t l = 0;
    static const uint8_t sec_btn[S_COUNT] = { TFM_BTN_HOME, TFM_BTN_EDIT, TFM_BTN_ENV, TFM_BTN_SEL, TFM_BTN_GLO, 0, TFM_BTN_FX, TFM_BTN_LFO };
    if (u->section == S_INFO) l |= 1ull << u->info;
    else l |= 1ull << sec_btn[u->section];
    if (u->octave < 0) l |= 1ull << TFM_BTN_OCT_DOWN;
    if (u->octave > 0) l |= 1ull << TFM_BTN_OCT_UP;
    if (u->section == S_EDIT || u->section == S_ENV) l |= 1ull << (14 + black_keys[u->op]);
    for (unsigned k = 0; k < TFM_KEYS; k++) if (u->key_note[k] != 0xff) l |= 1ull << (14 + k);
    return l;
}

/* ------------------------------------------------------------------ */
/* Primitive di disegno, ritagliate sulla banda corrente               */
/* ------------------------------------------------------------------ */

/* lo schermo dell'FM-1 vuole i pixel RGB565 con i byte scambiati */
#ifdef TFM_UI_BYTESWAP
#define PXC(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))
#else
#define PXC(c) (c)
#endif
static void fill(tfm_ui *u, int x, int y, int w, int h, uint16_t c)
{
    c = PXC(c);
    int top = (int)u->band_top, bot = top + 16;
    if (x < 0) { w += x; x = 0; }
    if (x + w > TFM_SCREEN_W) w = TFM_SCREEN_W - x;
    int y0 = y < top ? top : y, y1 = y + h > bot ? bot : y + h;
    if (w <= 0 || y0 >= y1) return;
    for (int r = y0; r < y1; r++) {
        uint16_t *p = u->band + (r - top) * TFM_SCREEN_W + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}
static inline void pix(tfm_ui *u, int x, int y, uint16_t c)
{
    if ((unsigned)x < TFM_SCREEN_W && y >= (int)u->band_top && y < (int)u->band_top + 16)
        u->band[(y - (int)u->band_top) * TFM_SCREEN_W + x] = PXC(c);
}
static void line(tfm_ui *u, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pix(u, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void frame(tfm_ui *u, int x, int y, int w, int h, uint16_t c)
{
    fill(u, x, y, w, 1, c); fill(u, x, y + h - 1, w, 1, c);
    fill(u, x, y, 1, h, c); fill(u, x + w - 1, y, 1, h, c);
}
static const uint8_t *glyph(char ch)
{
    static const uint8_t blank[5] = { 0 };
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= '0' && ch <= '9') return font[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return font[10 + ch - 'A'];
    for (unsigned i = 0; i < sizeof punct / sizeof punct[0]; i++)
        if ((uint8_t)ch == punct[i][0]) return punct[i] + 1;
    return blank;
}
static int text(tfm_ui *u, int x, int y, const char *s, int scale, uint16_t c)
{
    int top = (int)u->band_top;
    if (y >= top + 16 || y + 7 * scale <= top) return x + (int)tfm_strlen(s) * 6 * scale;
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1u << row)) fill(u, x + col * scale, y + row * scale, scale, scale, c);
    }
    return x;
}
static int text_w(const char *s, int scale) { return (int)tfm_strlen(s) * 6 * scale; }

/* sin(2*pi*t) approssimato (Bhaskara), t in [0,1) */
static float sin1(float t)
{
    t -= (float)(int)t;
    float x = t < 0.5f ? t : t - 0.5f;
    float q = x * (1.f - 2.f * x);
    float y = 32.f * q / (5.f - 8.f * q);
    return t < 0.5f ? y : -y;
}
/* forma delle 8 onde OPL3 (per il disegno) */
static float wave_shape(unsigned w, float t)
{
    float s = sin1(t), h = t < 0.5f ? 1.f : 0.f;
    switch (w) {
    case 1: return h > 0 ? s : 0.f;
    case 2: return s < 0 ? -s : s;
    case 3: { float q = t - (float)(int)(t * 2.f) * 0.5f; return q < 0.25f ? sin1(q) : 0.f; }
    case 4: return t < 0.5f ? sin1(t * 2.f) : 0.f;
    case 5: { float v = sin1(t * 2.f); return t < 0.5f ? (v < 0 ? -v : v) : 0.f; }
    case 6: return t < 0.5f ? 1.f : -1.f;
    case 7: { float q = t < 0.5f ? t * 2.f : (1.f - t) * 2.f; float v = 1.f;
              for (int i = 0; i < 6; i++) { if (q > 0.12f * (float)(i + 1)) v *= 0.55f; }
              return t < 0.5f ? v : -v; }
    default: return s;
    }
}

/* ------------------------------------------------------------------ */
/* Pagine                                                              */
/* ------------------------------------------------------------------ */

static void header(tfm_ui *u, const char *title, const char *right)
{
    fill(u, 0, 0, 240, 28, PANEL);
    text(u, 6, 7, title, 2, AMBER);
    if (right) text(u, 234 - text_w(right, 1), 11, right, 1, ACCENT);
}
static void footer(tfm_ui *u, const pdesc *ps)
{
    for (int k = 0; k < 4; k++) {
        int x = 6 + 59 * k;
        const pdesc *d = &ps[k];
        char v[12];
        value_text(u, d, v);
        int active = d->type != T_NONE && resolve_id(u, d) >= 0;
        uint16_t col = active ? knob_color[k] : DIM;
        fill(u, x, 195, 54, 2, col);
        if (u->touched == k) fill(u, x, 197, 54, 30, PANEL);
        text(u, x, 200, d->label, 1, active ? TEXT : DIM);
        text(u, x, 212, v, tfm_strlen(v) <= 4 ? 2 : 1, col);
    }
}
static void page_dots(tfm_ui *u, unsigned n, unsigned cur)
{
    if (n < 2) return;
    for (unsigned i = 0; i < n; i++) fill(u, 234 - (int)(n - i) * 9, 184, 6, 6, i == cur ? TEXT : DIM);
}
/* operatori che suonano (portanti) per ogni algoritmo, bit 0 = OP1 */
static const uint8_t carriers[TFM_ALGOS] = { 0x08, 0x09, 0x0a, 0x0d, 0x0f, 0x08, 0x0e, 0x08, 0x0c, 0x0e };

/* Schema dell'algoritmo: per ogni operatore colonna (0 = modulatore piu'
 * lontano, 3 = uscita) e corsia; poi i collegamenti sorgente->destinazione. */
static const uint8_t alg_pos[TFM_ALGOS][4][2] = {
    {{0,0},{1,0},{2,0},{3,0}}, {{3,1},{1,0},{2,0},{3,0}}, {{2,0},{3,0},{2,1},{3,1}}, {{3,0},{2,1},{3,1},{3,2}},
    {{3,0},{3,1},{3,2},{3,3}}, {{1,0},{1,1},{2,0},{3,0}}, {{2,1},{3,0},{3,1},{3,2}}, {{2,0},{2,1},{2,2},{3,1}},
    {{1,0},{2,0},{3,0},{3,1}}, {{2,0},{3,0},{3,1},{3,2}} };
static const uint8_t alg_edges[TFM_ALGOS][4] = { /* coppie sorgente*16+destinazione, 0 = fine */
    {0x12,0x23,0x34,0}, {0x23,0x34,0,0}, {0x12,0x34,0,0}, {0x23,0,0,0}, {0,0,0,0},
    {0x13,0x23,0x34,0}, {0x12,0x13,0x14,0}, {0x14,0x24,0x34,0}, {0x12,0x23,0x24,0}, {0x12,0,0,0} };
enum { AB_W = 20, AB_H = 12, AC_W = 30, AL_H = 16 };
static void alg_box(tfm_ui *u, int x, int y, unsigned op, uint16_t c, int fb)
{
    frame(u, x, y, AB_W, AB_H, c);
    char s[2] = { (char)('0' + op), 0 };
    text(u, x + 7, y + 3, s, 1, c);
    if (fb) { fill(u, x + 3, y - 4, 14, 1, c); fill(u, x + 3, y - 4, 1, 4, c); fill(u, x + 16, y - 4, 1, 4, c); }
}
static int draw_algo(tfm_ui *u, int y)
{
    unsigned a = algo_cur(u);
    int x0 = 6, lanes = 1;
    for (unsigned i = 0; i < 4; i++) if (alg_pos[a][i][1] + 1 > lanes) lanes = alg_pos[a][i][1] + 1;
    for (unsigned e = 0; e < 4 && alg_edges[a][e]; e++) {
        unsigned s = (alg_edges[a][e] >> 4) - 1, d = (alg_edges[a][e] & 15) - 1;
        int sx = x0 + alg_pos[a][s][0] * AC_W + AB_W, sy = y + alg_pos[a][s][1] * AL_H + AB_H / 2;
        int dx = x0 + alg_pos[a][d][0] * AC_W, dy = y + alg_pos[a][d][1] * AL_H + AB_H / 2;
        line(u, sx, sy, dx - 1, dy, DIM);
    }
    for (unsigned i = 0; i < 4; i++) {
        uint16_t c = (carriers[a] >> i) & 1 ? GREEN : TEXT;
        alg_box(u, x0 + alg_pos[a][i][0] * AC_W, y + alg_pos[a][i][1] * AL_H, i + 1, c, i == 0 && u->patch.p[TFM_P_FB1]);
        if ((carriers[a] >> i) & 1) fill(u, x0 + alg_pos[a][i][0] * AC_W + AB_W, y + alg_pos[a][i][1] * AL_H + AB_H / 2, 4, 1, GREEN);
    }
    /* layer 2 a destra, dopo un separatore */
    int x = x0 + 4 * AC_W + 6;
    fill(u, x, y - 4, 1, lanes * AL_H + 2, DIM);
    x += 8;
    int par = u->patch.p[TFM_P_ALGO2] != 0;
    alg_box(u, x, y, 5, par ? GREEN : TEXT, u->patch.p[TFM_P_FB2] != 0);
    if (par) alg_box(u, x, y + AL_H, 6, GREEN, 0);
    else { line(u, x + AB_W, y + AB_H / 2, x + AC_W - 1, y + AB_H / 2, DIM); alg_box(u, x + AC_W, y, 6, GREEN, 0); }
    return y + (lanes > 2 ? lanes : 2) * AL_H;
}

static void draw_home(tfm_ui *u)
{
    char num[8] = "P01/05";
    num[1] = (char)('0' + (u->preset + 1) / 10); num[2] = (char)('0' + (u->preset + 1) % 10);
    num[4] = (char)('0' + TFM_PRESET_COUNT / 10); num[5] = (char)('0' + TFM_PRESET_COUNT % 10);
    header(u, "HOME", num);
    text(u, 6, 40, tfm_presets[u->preset].name, 3, TEXT);
    {
        unsigned a = algo_cur(u);
        char an[4] = { 'A', (char)('0' + (a + 1) / 10), (char)('0' + (a + 1) % 10), 0 };
        if (a + 1 < 10) { an[1] = (char)('1' + a); an[2] = 0; }
        int ax = text(u, 6, 68, "ALG", 1, DIM);
        ax = text(u, ax + 4, 68, an + 1, 1, AMBER);
        ax = text(u, ax + 6, 68, L_ALGO[a], 1, ACCENT);
        if (a >= TFM_ALGOS_ORIGINAL) text(u, ax + 6, 68, "EXTRA", 1, AMBER);
    }
    draw_algo(u, 84);
    const uint8_t *p = u->patch.p;
    unsigned ft = p[TFM_P_FLT_TYPE] > 14 ? 14 : p[TFM_P_FLT_TYPE];
    text(u, 6, 152, "FILTRO", 1, DIM);
    int x = text(u, 48, 150, L_FT[ft], 1, knob_color[1]);
    text(u, x + 8, 150, L_FLAV[p[TFM_P_FLAVOUR] ? 1 : 0], 1, p[TFM_P_FLAVOUR] ? AMBER : ACCENT);
    text(u, 6, 164, "VOCI", 1, DIM);
    {
        char vm[24];
        unsigned m = mode_cur(u);
        tfm_strcpy(vm, L_MODE[m]);
        if (mode_is_poly(u)) { unsigned n = (unsigned)tfm_strlen(vm); vm[n] = ' '; vm[n + 1] = (char)('0' + voices_cur(u)); vm[n + 2] = 'V'; vm[n + 3] = 0; }
        x = text(u, 48, 164, vm, 1, TEXT);
    }
    text(u, x + 12, 164, "OTT", 1, DIM);
    {
        uint8_t sl[4];
        fx_slots(u, sl);
        int fx = text(u, 6, 176, "FX", 1, DIM) + 18;
        for (int i = 0; i < 4; i++) {
            int f = sl[i];
            fx = text(u, fx, 176, L_SLOT[f], 1, f && u->patch.p[fx_mix_id[f - 1]] ? GREEN : PANEL);
            if (i < 3) fx = text(u, fx, 176, ">", 1, PANEL);
        }
    }
    char oc[3] = { u->octave < 0 ? '-' : '+', (char)('0' + (u->octave < 0 ? -u->octave : u->octave)), 0 };
    text(u, x + 36, 164, oc, 1, TEXT);
    footer(u, P_HOME);
}

static void draw_op_strip(tfm_ui *u, int y)
{
    /* sei operatori con barra di volume; selezionato evidenziato */
    unsigned a = algo_cur(u);
    unsigned car = carriers[a] | (u->patch.p[TFM_P_ALGO2] ? 0x30u : 0x20u);
    for (int i = 0; i < 6; i++) {
        int x = 6 + i * 39;
        int sel = i == u->op;
        uint16_t c = (car >> i) & 1 ? GREEN : TEXT;
        if (sel) fill(u, x, y, 36, 34, PANEL);
        char s[4] = { 'O', 'P', (char)('1' + i), 0 };
        text(u, x + 9, y + 4, s, 1, sel ? AMBER : c);
        int vol = u->patch.p[TFM_OP_BASE(i) + TFM_OPP_VOL];
        fill(u, x + 4, y + 18, 28, 8, BACK);
        frame(u, x + 4, y + 18, 28, 8, DIM);
        fill(u, x + 5, y + 19, vol * 26 / 127, 6, c);
    }
}

static void draw_edit(tfm_ui *u)
{
    char t[8] = "OP1";
    t[2] = (char)('1' + u->op);
    unsigned a = algo_cur(u);
    unsigned car = carriers[a] | (u->patch.p[TFM_P_ALGO2] ? 0x30u : 0x20u);
    header(u, "EDIT", (car >> u->op) & 1 ? "PORTANTE" : "MODULATORE");
    draw_op_strip(u, 34);
    text(u, 6, 78, t, 4, AMBER);
    unsigned w = u->patch.p[TFM_OP_BASE(u->op) + TFM_OPP_WAVE] >> 4;
    text(u, 6, 114, L_WAVE[w > 7 ? 7 : w], 2, knob_color[1]);
    /* disegno della forma d'onda */
    int bx = 92, by = 80, bw = 142, bh = 70, mid = by + bh / 2;
    frame(u, bx, by, bw, bh, PANEL);
    fill(u, bx + 1, mid, bw - 2, 1, PANEL);
    int py = mid;
    for (int i = 0; i < bw - 4; i++) {
        float v = wave_shape(w > 7 ? 7 : w, (float)i / (float)(bw - 4) * 2.f);
        int yy = mid - (int)(v * (float)(bh / 2 - 4));
        if (i) line(u, bx + 1 + i, py, bx + 2 + i, yy, knob_color[1]);
        py = yy;
    }
    text(u, 6, 140, "PAG", 1, DIM);
    text(u, 30, 140, u->page[S_EDIT] ? "2 SCALA" : "1 BASE", 1, TEXT);
    text(u, 6, 164, "TASTI NERI = OPERATORE", 1, DIM);
    page_dots(u, 2, u->page[S_EDIT]);
    footer(u, P_EDIT[u->page[S_EDIT]]);
}

/* inviluppo: segmenti proporzionali ai tempi */
static void draw_adsr(tfm_ui *u, int bx, int by, int bw, int bh, uint8_t a, uint8_t d, uint8_t s, uint8_t r, uint16_t c)
{
    frame(u, bx, by, bw, bh, PANEL);
    int wa = 4 + a * 40 / 127, wd = 4 + d * 40 / 127, wr = 4 + r * 40 / 127;
    int ws = bw - 8 - wa - wd - wr;
    if (ws < 10) ws = 10;
    int x0 = bx + 4, yb = by + bh - 4, yt = by + 4;
    int ys = yb - (int)s * (bh - 8) / 127;
    line(u, x0, yb, x0 + wa, yt, c);
    line(u, x0 + wa, yt, x0 + wa + wd, ys, c);
    line(u, x0 + wa + wd, ys, x0 + wa + wd + ws, ys, c);
    line(u, x0 + wa + wd + ws, ys, x0 + wa + wd + ws + wr, yb, c);
}

static void draw_env(tfm_ui *u)
{
    char t[8] = "OP1";
    t[2] = (char)('1' + u->op);
    header(u, "ENV", t);
    draw_op_strip(u, 34);
    const uint8_t *q = u->patch.p + TFM_OP_BASE(u->op);
    draw_adsr(u, 6, 80, 228, 92, q[TFM_OPP_A], q[TFM_OPP_D], q[TFM_OPP_S], q[TFM_OPP_R], knob_color[3]);
    text(u, 10, 178, "SELECT/TASTI NERI = OPERATORE", 1, DIM);
    footer(u, P_ENV);
}

/* risposta in frequenza del filtro (stessa formula di pole mixing del motore) */
static const float mixc[15][6] = {
    {0,0,0,0,1,1},{0,0,0,1,0,1},{0,0,1,0,0,1},{0,1,0,0,0,1},{0,0,1,-2,1,3},{0,1,-1,0,0,2},{1,-2,2,0,0,1},{1,-3,6,-4,0,1},
    {1,-1,0,0,0,1},{1,-2,1,0,0,1},{1,-3,3,-1,0,1},{0,1,-2,1,0,1.4f},{0,1,-3,3,-1,1.4f},{0,1,-2,2,0,1},{0,1,-3,6,-4,1}};
static void draw_response(tfm_ui *u, int bx, int by, int bw, int bh)
{
    const uint8_t *p = u->patch.p;
    unsigned m = p[TFM_P_FLT_TYPE] > 14 ? 14 : p[TFM_P_FLT_TYPE];
    float res = (float)p[TFM_P_RES] / 127.f;
    float k = p[TFM_P_FLAVOUR] ? res * 5.6f : res * 4.05f;
    if (k > 3.7f) k = 3.7f; /* il disegno resta leggibile anche in autooscillazione */
    float fc_oct = (float)p[TFM_P_CUTOFF] * (10.f / 127.f); /* 20 Hz * 2^oct */
    frame(u, bx, by, bw, bh, PANEL);
    int zero = by + bh / 3;
    fill(u, bx + 1, zero, bw - 2, 1, PANEL);
    int py = 0;
    for (int i = 0; i < bw - 2; i++) {
        float oct = (float)i / (float)(bw - 2) * 10.f;   /* 20 Hz .. 20 kHz */
        float wr = 1.f, d = oct - fc_oct;
        /* w = 2^d calcolato a mano */
        if (d > 0) { while (d >= 1.f) { wr *= 2.f; d -= 1.f; } wr *= 1.f + d * (0.6931f + d * 0.2402f); }
        else { while (d <= -1.f) { wr *= 0.5f; d += 1.f; } wr *= 1.f + d * (0.6931f + d * 0.2402f); }
        /* p = 1/(1+jw) */
        float den = 1.f + wr * wr, pr = 1.f / den, pi = -wr / den;
        float powr[5], powi[5];
        powr[0] = 1.f; powi[0] = 0.f;
        for (int n = 1; n < 5; n++) { powr[n] = powr[n-1] * pr - powi[n-1] * pi; powi[n] = powr[n-1] * pi + powi[n-1] * pr; }
        /* y0 = 1/(1 + k p^4) */
        float dr = 1.f + k * powr[4], di = k * powi[4], dd = dr * dr + di * di;
        float yr = dr / dd, yi = -di / dd;
        float hr = 0, hi = 0;
        for (int n = 0; n < 5; n++) { hr += mixc[m][n] * powr[n]; hi += mixc[m][n] * powi[n]; }
        float g = mixc[m][5] * (1.f + 0.5f * k);
        float orr = (hr * yr - hi * yi) * g, oi = (hr * yi + hi * yr) * g;
        float mag2 = orr * orr + oi * oi;
        /* dB approssimati: 10*log10(mag2) via log2 dell'esponente */
        float db = -60.f;
        if (mag2 > 1e-6f) {
            int e = 0; float mm = mag2;
            while (mm >= 2.f) { mm *= 0.5f; e++; }
            while (mm < 1.f) { mm *= 2.f; e--; }
            float l2 = (float)e + (mm - 1.f) * (1.4427f - 0.4427f * (mm - 1.f));
            db = 3.0103f * l2;
        }
        int yy = zero - (int)(db * (float)(bh / 3) / 18.f);
        if (yy < by + 1) yy = by + 1;
        if (yy > by + bh - 2) yy = by + bh - 2;
        if (i) line(u, bx + i, py, bx + 1 + i, yy, knob_color[1]);
        py = yy;
    }
}

static void draw_filter(tfm_ui *u)
{
    static const char *const sub[3] = { "1 FILTRO", "2 CARATTERE", "3 INVILUPPO" };
    const uint8_t *p = u->patch.p;
    header(u, "FILTRO", p[TFM_P_FLAVOUR] ? "MS20" : "LIQUID");
    if (u->page[S_FILTER] == 2)
        draw_adsr(u, 6, 36, 228, 128, p[TFM_P_FLT_A], p[TFM_P_FLT_D], p[TFM_P_FLT_S], p[TFM_P_FLT_R], knob_color[3]);
    else
        draw_response(u, 6, 36, 228, 128);
    text(u, 6, 172, sub[u->page[S_FILTER]], 1, TEXT);
    page_dots(u, 3, u->page[S_FILTER]);
    footer(u, P_FILTER[u->page[S_FILTER]]);
}

/* ---- modulazioni ---- */
/* numero con decimali (es. 2.35) */
static void fmt_fixed(char *out, float v, unsigned dec)
{
    unsigned scale = dec == 2 ? 100u : dec == 1 ? 10u : 1u;
    unsigned n = (unsigned)(v * (float)scale + 0.5f), ip = n / scale, fp = n % scale;
    char tmp[8]; int k = 0;
    do { tmp[k++] = (char)('0' + ip % 10); ip /= 10; } while (ip && k < 6);
    int o = 0;
    while (k) out[o++] = tmp[--k];
    if (dec) {
        out[o++] = '.';
        if (dec == 2) out[o++] = (char)('0' + fp / 10);
        out[o++] = (char)('0' + fp % 10);
    }
    out[o] = 0;
}
/* stessa scala del motore: 0.03 Hz * 2^(v * 9.38 / 127) */
static float lfo_rate_hz(uint8_t v) { float h = 0.03f; for (unsigned k = 0; k < v; k++) h *= 1.05252f; return h; }
static float slow_rate_hz(uint8_t v) { float h = 0.5f; for (unsigned k = 0; k < v; k++) h *= 1.02543f; return h; }  /* vibrato e tremolo */
/* forme degli LFO (come nel motore), valore -1..1; il NOIZ disegnato a gradini fissi */
static float lfo_draw(unsigned shape, unsigned mode, float t)
{
    t -= (float)(int)t;
    float s;
    switch (shape) {
    case 0: s = 1.f - 4.f * (t < 0.5f ? 0.5f - t : t - 0.5f); break;
    case 1: s = 2.f * t - 1.f; break;
    case 2: s = 1.f - 2.f * t; break;
    case 3: s = t < 0.5f ? 1.f : -1.f; break;
    case 4: { static const int8_t st[8] = { 3, -7, 6, -2, 8, -5, 1, -8 }; s = (float)st[(int)(t * 8.f) & 7] * 0.125f; } break;
    default: s = sin1(t); break;
    }
    switch (mode) {
    case 1: return 0.5f + 0.5f * s;
    case 2: return -(0.5f + 0.5f * s);
    case 3: return s < 0 ? -s : s;
    case 4: return s < 0 ? s : -s;
    default: return s;
    }
}
static void draw_curve(tfm_ui *u, int bx, int by, int bw, int bh, unsigned shape, unsigned mode, float cycles, uint16_t c)
{
    int mid = by + bh / 2;
    frame(u, bx, by, bw, bh, PANEL);
    fill(u, bx + 1, mid, bw - 2, 1, PANEL);
    int py = mid;
    for (int i = 0; i < bw - 4; i++) {
        float v = lfo_draw(shape, mode, (float)i / (float)(bw - 4) * cycles);
        int yy = mid - (int)(v * (float)(bh / 2 - 4));
        if (i) line(u, bx + 1 + i, py, bx + 2 + i, yy, c);
        py = yy;
    }
}
/* elenco dei 4 collegamenti di una chain, con barra della quantita' */
static void draw_links(tfm_ui *u, int y, unsigned base, int hl)
{
    const uint8_t *p = u->patch.p;
    for (unsigned i = 0; i < TFM_LINKS; i++) {
        int yy = y + (int)i * 18;
        unsigned t = p[base + 2 * i] < TFM_MOD_TARGETS ? p[base + 2 * i] : 0;
        int a = (int)(p[base + 2 * i + 1] > 40 ? 40 : p[base + 2 * i + 1]) - TFM_AMT_ZERO;
        int cur = hl >= 0 && (int)(i / 2) == hl;
        uint16_t c = t && a ? GREEN : DIM;
        if (cur) fill(u, 4, yy - 3, 232, 16, PANEL);
        char n[2] = { (char)('1' + i), 0 };
        text(u, 8, yy + 2, n, 1, cur ? AMBER : DIM);
        text(u, 20, yy + 2, tfm_mod_target_names[t], 1, t ? TEXT : DIM);
        int cx = 168;
        fill(u, cx - 60, yy + 5, 121, 1, DIM);
        fill(u, cx, yy, 1, 11, DIM);
        int w = a * 3;
        if (w > 0) fill(u, cx + 1, yy + 2, w, 7, c);
        if (w < 0) fill(u, cx + w, yy + 2, -w, 7, c);
    }
}
static void draw_lfo(tfm_ui *u)
{
    static const char *const sub[3] = { "PRINCIPALE", "CHAIN 1-2", "CHAIN 3-4" };
    const uint8_t *p = u->patch.p;
    unsigned pg = u->page[S_LFO], k = pg / 3, sp = pg % 3;
    char title[8] = "LFO 1";
    if (k < 3) title[4] = (char)('1' + k);
    header(u, k < 3 ? title : "MOD ENV", sub[sp]);
    if (k < 3) {
        const uint8_t *q = p + TFM_LFO_BASE(k);
        unsigned sh = q[1] % TFM_LFO_SHAPES, md = q[2] % TFM_LFO_MODES;
        if (sp == 0) {
            draw_curve(u, 6, 36, 228, 92, sh, md, 2.f, knob_color[k]);
            char hz[12];
            fmt_fixed(hz, lfo_rate_hz(q[0]), 2);
            int x = text(u, 6, 140, hz, 2, TEXT);
            text(u, x + 4, 147, "HZ", 1, DIM);
            text(u, 120, 140, q[3] ? "RETRIG" : "LIBERO", 2, q[3] ? AMBER : DIM);
            /* collegamenti attivi in breve */
            unsigned n = 0;
            for (unsigned i = 0; i < TFM_LINKS; i++)
                if (p[TFM_LFO_LINK(k, i)] && p[TFM_LFO_LINK(k, i) + 1] != TFM_AMT_ZERO) n++;
            char s[16] = "CHAIN: 0 ATTIVE";
            s[7] = (char)('0' + n);
            text(u, 6, 168, s, 1, n ? GREEN : DIM);
        } else {
            draw_curve(u, 6, 34, 228, 40, sh, md, 3.f, knob_color[k]);
            draw_links(u, 84, TFM_LFO_LINK(k, 0), (int)sp - 1);
        }
    } else {
        if (sp == 0) draw_adsr(u, 6, 36, 228, 128, p[TFM_P_MENV_A], p[TFM_P_MENV_D], p[TFM_P_MENV_S], p[TFM_P_MENV_R], knob_color[3]);
        else {
            draw_adsr(u, 6, 34, 228, 40, p[TFM_P_MENV_A], p[TFM_P_MENV_D], p[TFM_P_MENV_S], p[TFM_P_MENV_R], knob_color[3]);
            draw_links(u, 84, TFM_MENV_LINK(0), (int)sp - 1);
        }
    }
    page_dots(u, 3, sp);
    footer(u, P_LFO[pg]);
}

static void draw_glo_mod(tfm_ui *u, unsigned pg)
{
    const uint8_t *p = u->patch.p;
    char s[12];
    if (pg == 3) {   /* vibrato */
        static const uint8_t vshape[5] = { 5, 3, 1, 2, 4 };
        unsigned vm = p[TFM_P_VIB_MODE] % 3;
        draw_curve(u, 6, 36, 228, 92, vshape[p[TFM_P_VIB_WAVE] % 5], vm == 1 ? 1 : vm == 2 ? 2 : 0, 4.f, knob_color[1]);
        fmt_fixed(s, slow_rate_hz(p[TFM_P_VIB_RATE]), 1);
        int x = text(u, 6, 140, s, 2, TEXT);
        text(u, x + 4, 147, "HZ", 1, DIM);
        float d = (float)p[TFM_P_VIB_DEPTH] / 127.f;
        fmt_fixed(s, d * d * 200.f, 0);
        x = text(u, 120, 140, s, 2, p[TFM_P_VIB_DEPTH] ? AMBER : DIM);
        text(u, x + 4, 147, "CENT", 1, DIM);
    } else if (pg == 4) {   /* tremolo: L e R sfasati */
        int bx = 6, by = 36, bw = 228, bh = 92;
        float d = (float)p[TFM_P_TREM_DEPTH] / 127.f, off = (float)p[TFM_P_TREM_PHASE] * (0.5f / 127.f);
        frame(u, bx, by, bw, bh, PANEL);
        for (int ch = 0; ch < 2; ch++) {
            int py = 0;
            for (int i = 0; i < bw - 4; i++) {
                float t = (float)i / (float)(bw - 4) * 3.f + (ch ? off : 0.f);
                float g = 1.f - d * (0.5f + 0.5f * sin1(t));
                int yy = by + bh - 4 - (int)(g * (float)(bh - 10));
                if (i) line(u, bx + 1 + i, py, bx + 2 + i, yy, knob_color[ch ? 3 : 1]);
                py = yy;
            }
        }
        text(u, bx + 6, by + 6, "L", 1, knob_color[1]);
        text(u, bx + 18, by + 6, "R", 1, knob_color[3]);
        fmt_fixed(s, slow_rate_hz(p[TFM_P_TREM_RATE]), 1);
        int x = text(u, 6, 140, s, 2, TEXT);
        text(u, x + 4, 147, "HZ", 1, DIM);
        fmt_fixed(s, (float)p[TFM_P_TREM_PHASE] * (180.f / 127.f), 0);
        x = text(u, 120, 140, s, 2, TEXT);
        text(u, x + 4, 147, "GRADI L/R", 1, DIM);
    } else {   /* pitch: glide, rise, fall */
        int bx = 6, by = 36, bw = 228, bh = 92;
        frame(u, bx, by, bw, bh, PANEL);
        int lo = by + bh - 14, hi = by + 24;
        float gl = (float)p[TFM_P_GLIDE] / 127.f, rise = (float)p[TFM_P_RISE] / 127.f, fall = (float)p[TFM_P_FALL] / 127.f;
        int x1 = bx + 70, xr = bx + 160;
        line(u, bx + 4, lo, x1, lo, DIM);   /* nota precedente */
        /* glide verso la nuova nota (piu' lungo con GLIDE), con il rise che parte piu' in basso */
        int gw = 2 + (int)(gl * 60.f), py = lo;
        for (int i = 0; i <= xr - x1; i++) {
            float tg = gw > 2 ? (float)i / (float)gw : 9.f;
            float e = tg > 3.f ? 0.f : 1.f;
            for (int m = 0; m < 3 && tg <= 3.f; m++) e *= 1.f - tg / 3.f;
            float r = (float)i / 18.f;
            float re = r > 3.f ? 0.f : (1.f - r / 3.f) * (1.f - r / 3.f);
            int yy = hi + (int)((float)(lo - hi) * e) + (int)(rise * 40.f * re);
            if (yy > by + bh - 3) yy = by + bh - 3;
            if (i) line(u, x1 + i - 1, py, x1 + i, yy, knob_color[0]);
            py = yy;
        }
        /* fall dopo il rilascio */
        for (int i = 0; i < bx + bw - 4 - xr; i++) {
            float f = (float)i / (8.f + fall * 30.f);
            int yy = hi + (int)(fall * 50.f * (1.f - 1.f / (1.f + f)));
            if (yy > by + bh - 3) yy = by + bh - 3;
            if (i) line(u, xr + i - 1, py, xr + i, yy, knob_color[3]);
            py = yy;
        }
        fill(u, xr, by + 2, 1, bh - 4, PANEL);
        text(u, xr + 4, by + 6, "NOTE OFF", 1, DIM);
        text(u, 6, 140, p[TFM_P_GLIDE_MODE] ? "GLIDE LEGATO" : "GLIDE SEMPRE", 2, p[TFM_P_GLIDE] ? AMBER : DIM);
    }
    page_dots(u, GLO_PAGES, pg);
    footer(u, P_GLO[pg]);
}

static void draw_glo(tfm_ui *u)
{
    static const char *const sub[GLO_PAGES] = { "1 VOCI", "2 LAYER", "3 LIVELLO", "4 VIBRATO", "5 TREMOLO", "6 PITCH" };
    header(u, "GLOBAL", sub[u->page[S_GLO]]);
    if (u->page[S_GLO] >= 3) { draw_glo_mod(u, u->page[S_GLO]); return; }
    const uint8_t *p = u->patch.p;
    unsigned m = mode_cur(u);
    /* le 8 voci: quadratini accesi in base al modo e al limite di polifonia */
    text(u, 6, 36, "VOCI", 1, DIM);
    unsigned group = m == 1 ? 8 : m == 2 ? 2 : m == 3 ? 4 : m == 4 ? 2 : 1;
    unsigned used = mode_is_poly(u) ? voices_cur(u) : 8;
    if (m == 4) used = 2;
    for (unsigned i = 0; i < 8; i++) {
        int x = 6 + (int)i * 29;
        uint16_t c = i < used ? knob_color[(i / group) & 3] : PANEL;
        fill(u, x, 48, 25, 18, c);
        char s1[2] = { (char)('1' + i), 0 };
        text(u, x + 10, 54, s1, 1, i < used ? BACK : DIM);
    }
    text(u, 6, 74, L_MODE_LONG[m], 2, TEXT);
    if (mode_is_mono(u)) { text(u, 6, 94, "PRIORITA", 1, DIM); text(u, 60, 94, L_PRIO[p[TFM_P_PRIO] > 2 ? 0 : p[TFM_P_PRIO]], 1, AMBER); }
    else { text(u, 6, 94, "NOTE INSIEME", 1, DIM); char n[2] = { (char)('0' + (m == 2 ? 4 : m == 3 ? 2 : used)), 0 }; text(u, 84, 94, n, 1, AMBER); }
    char tv[8];
    value_text(u, &P_GLO[1][3], tv);
    text(u, 6, 120, "LAYER 2", 1, DIM);
    int x = text(u, 6, 134, p[TFM_P_ALGO2] ? "PARALLELO" : "SERIE", 2, TEXT);
    x = text(u, x + 12, 134, tv, 2, knob_color[3]);
    text(u, x + 4, 141, "ST", 1, DIM);
    page_dots(u, GLO_PAGES, u->page[S_GLO]);
    footer(u, P_GLO[u->page[S_GLO]]);
}


/* ---- effetti ---- */
static const char *const fx_page_name[7] = { "1 DIST", "2 PHASER", "3 TAPE ECHO", "4 NASTRO", "5 REVERB", "6 REVERB+", "7 CATENA" };
/* slot della catena */
static void fx_slots(const tfm_ui *u, uint8_t out[4])
{
    const uint8_t *p = u->patch.p;
    for (int i = 0; i < 4; i++) out[i] = p[TFM_P_FX_SLOT1 + i] <= 4 ? p[TFM_P_FX_SLOT1 + i] : 0;
}
static void fx_chain(tfm_ui *u, int y, int current)
{
    uint8_t sl[4];
    fx_slots(u, sl);
    for (int i = 0; i < 4; i++) {
        int x = 6 + i * 58;
        int f = sl[i];
        int on = f && u->patch.p[fx_mix_id[f - 1]] != 0;
        int cur = f && f - 1 == current;
        uint16_t c = on ? GREEN : DIM;
        if (cur) fill(u, x, y, 50, 16, PANEL);
        frame(u, x, y, 50, 16, cur ? AMBER : c);
        text(u, x + 25 - text_w(L_SLOT[f], 1) / 2, y + 5, L_SLOT[f], 1, cur ? AMBER : c);
        if (i < 3) fill(u, x + 50, y + 8, 8, 1, DIM);
    }
}
static void draw_fx(tfm_ui *u)
{
    unsigned pg = u->page[S_FX];
    static const uint8_t page_fx[7] = { 0, 1, 2, 2, 3, 3, 9 };
    int cur = page_fx[pg];
    const uint8_t *p = u->patch.p;
    header(u, "FX", fx_page_name[pg]);
    if (pg == 6) { /* pagina catena: grande, con ingresso e uscita */
        uint8_t sl[4];
        fx_slots(u, sl);
        text(u, 6, 40, "SYNTH", 1, DIM);
        for (int i = 0; i < 4; i++) {
            int y = 54 + i * 26, f = sl[i];
            int on = f && p[fx_mix_id[f - 1]] != 0;
            uint16_t c = f ? (on ? GREEN : TEXT) : DIM;
            fill(u, 20, y - 6, 1, 6, DIM);
            fill(u, 6, y, 6, 14, knob_color[i]);
            char k[3] = { 'K', (char)('1' + i), 0 };
            text(u, 16, y + 4, k, 1, knob_color[i]);
            frame(u, 36, y, 120, 18, c);
            static const char *const longn[5] = { "VUOTO", "DISTORSIONE", "PHASER", "TAPE ECHO", "REVERB" };
            text(u, 42, y + 6, longn[f], 1, c);
            text(u, 164, y + 6, f ? (on ? "ACCESO" : "MIX 0") : "", 1, on ? GREEN : DIM);
        }
        text(u, 6, 160, "USCITA", 1, DIM);
        text(u, 60, 172, "STESSO EFFETTO = SCAMBIO", 1, DIM);
        page_dots(u, 7, pg);
        footer(u, P_FX[pg]);
        return;
    }
    fx_chain(u, 34, cur);
    int bx = 6, by = 58, bw = 228, bh = 96;
    frame(u, bx, by, bw, bh, PANEL);
    if (cur == 0) { /* curva di trasferimento */
        unsigned s = p[TFM_P_DIST_TYPE] < TFM_DIST_SHAPES ? p[TFM_P_DIST_TYPE] : 0;
        float d = (float)p[TFM_P_DIST_DRIVE] / 127.f;
        int cx = bx + bw / 2, cy = by + bh / 2;
        fill(u, bx + 1, cy, bw - 2, 1, PANEL); fill(u, cx, by + 1, 1, bh - 2, PANEL);
        int py = cy;
        for (int i = 0; i < 120; i++) {
            float x = ((float)i / 119.f) * 2.f - 1.f;
            float y = tfm_fx_dist_curve(s, x * 4.f, d);
            if (y > 1.f) y = 1.f;
            if (y < -1.f) y = -1.f;
            int yy = cy - (int)(y * (float)(bh / 2 - 4));
            int xx = cx - 60 + i;
            if (i) line(u, xx - 1, py, xx, yy, knob_color[0]);
            py = yy;
        }
        text(u, bx + 6, by + 6, L_DIST_LONG[s], 2, TEXT);
    } else if (cur == 1) { /* risposta del phaser: tacche nello spettro */
        float depth = (float)p[TFM_P_PH_DEPTH] / 127.f;
        float fc_oct = 3.2f + depth * 2.2f; /* centro della spazzata, in ottave sopra 20 Hz */
        int py = 0;
        for (int i = 0; i < bw - 2; i++) {
            float oct = (float)i / (float)(bw - 2) * 10.f;
            float r = 1.f, dd = oct - fc_oct; /* rapporto f/fc = 2^dd */
            if (dd > 0) { while (dd >= 1.f) { r *= 2.f; dd -= 1.f; } } else { while (dd <= -1.f) { r *= 0.5f; dd += 1.f; } }
            r *= 1.f + dd * (0.6931f + dd * 0.2402f);
            float at = r < 1.f ? r / (1.f + 0.28f * r * r) : 1.5708f - r / (r * r + 0.28f);
            float ph = 6.f * 2.f * at; /* fase totale dei 6 stadi */
            float c = sin1(ph / 6.2832f + 0.25f);  /* cos(ph) */
            float mag = 0.5f * (1.f + c);           /* |1 + e^(j ph)| / 2, al quadrato */
            int yy = by + bh - 4 - (int)(mag * (float)(bh - 10));
            if (i) line(u, bx + i, py, bx + 1 + i, yy, knob_color[1]);
            py = yy;
        }
        text(u, bx + 6, by + 6, "6 STADI STEREO", 1, TEXT);
    } else if (cur == 2) { /* ripetizioni dell'eco */
        float ms = 30.f; for (unsigned k = 0; k < p[TFM_P_DLY_TIME]; k++) ms *= 1.02511f;
        float fb = (float)p[TFM_P_DLY_FB] / 127.f * 1.03f, a = 1.f;
        int base = by + bh - 6;
        fill(u, bx + 4, base - (bh - 20), 4, bh - 20, TEXT);
        for (int k = 1; k < 30; k++) {
            int x = bx + 4 + (int)(ms * (float)k * (float)(bw - 12) / 1500.f);
            if (x > bx + bw - 6) break;
            a *= fb;
            if (a > 1.f) a = 1.f;
            int h = (int)(a * (float)(bh - 20));
            if (h < 1) break;
            fill(u, x, base - h, 4, h, knob_color[2]);
        }
        text(u, bx + 6, by + 6, "1.5 SECONDI", 1, DIM);
    } else { /* coda del riverbero */
        unsigned t = p[TFM_P_REV_TYPE] < TFM_REV_TYPES ? p[TFM_P_REV_TYPE] : 0;
        float dec = (float)p[TFM_P_REV_DECAY] / 127.f;
        float len = (t == 0 ? 0.35f : t == 1 ? 0.6f : 1.f) * (0.3f + 0.7f * dec);
        int pre = (int)((float)p[TFM_P_REV_PREDLY] / 127.f * 30.f);
        int py = by + bh - 4, x0 = bx + 4 + pre;
        line(u, bx + 4, by + bh - 4, x0, by + bh - 4, DIM);
        for (int i = 0; i < bw - 10 - pre; i++) {
            float f = (float)i / ((float)(bw - 10) * len);
            float a = 1.f / (1.f + f * f * 6.f + f * 2.f);
            int yy = by + bh - 4 - (int)(a * (float)(bh - 24));
            int noise = ((i * 37) % 7) - 3;
            if (a > 0.02f) yy += noise * (int)(a * 3.f) / 3;
            if (i) line(u, x0 + i - 1, py, x0 + i, yy, knob_color[3]);
            py = yy;
        }
        text(u, bx + 6, by + 6, L_REV_LONG[t], 2, TEXT);
    }
    text(u, 6, 164, "SELECT = PAGINA", 1, DIM);
    page_dots(u, 7, pg);
    footer(u, P_FX[pg]);
}

static void draw_info(tfm_ui *u)
{
    static const char *const names[TFM_BTN_COUNT] = { "", "", "", "", "", "", "", "", "", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
    static const char *const what[TFM_BTN_COUNT] = { "", "", "", "", "", "", "", "", "", "SALVA PRESET",
                                                     "ARPEGGIATORE", "SEQUENCER 128", "SEQUENCER", "REGISTRA STEP" };
    static const char *const phase[TFM_BTN_COUNT] = { "", "", "", "", "", "", "", "", "", "FASE 5", "FASE 5", "FASE 5", "FASE 5", "FASE 5" };
    unsigned b = u->info < TFM_BTN_COUNT ? u->info : 0;
    header(u, names[b], 0);
    text(u, 6, 60, what[b], 2, TEXT);
    text(u, 6, 92, "IN ARRIVO:", 2, DIM);
    text(u, 6, 116, phase[b], 3, AMBER);
    text(u, 6, 164, "HOME PER TORNARE", 1, DIM);
}

static void scene(tfm_ui *u)
{
    fill(u, 0, (int)u->band_top, 240, 16, BACK);
    switch (u->section) {
    case S_EDIT: draw_edit(u); break;
    case S_ENV: draw_env(u); break;
    case S_FILTER: draw_filter(u); break;
    case S_GLO: draw_glo(u); break;
    case S_INFO: draw_info(u); break;
    case S_FX: draw_fx(u); break;
    case S_LFO: draw_lfo(u); break;
    default: draw_home(u); break;
    }
}

uint32_t tfm_ui_draw(tfm_ui *u)
{
    if (!u->dirty) return 0;
    u->dirty = 0;
    int y0 = -1, y1 = -1;
    for (unsigned b = 0; b < TFM_SCREEN_H / 16; b++) {
        u->band_top = b * 16;
        u->band = u->bands[u->which];
        scene(u);
        uint32_t h = 2166136261u;
        for (unsigned i = 0; i < TFM_SCREEN_W * 16; i++) h = (h ^ u->band[i]) * 16777619u;
        if (h == u->band_hash[b]) continue;  /* banda invariata: niente trasferimento */
        u->band_hash[b] = h;
#ifndef TFM_UI_NO_FRAMEBUFFER
        tfm_memcpy(u->fb + b * 16 * TFM_SCREEN_W, u->band, sizeof u->bands[0]);
#endif
        if (u->blit) {
            /* allo schermo (DMA); la prossima banda va nell'altro buffer, mai in quello in volo */
            u->blit(0, b * 16, TFM_SCREEN_W, 16, u->band);
            u->which ^= 1u;
        }
        if (y0 < 0) y0 = (int)b * 16;
        y1 = (int)b * 16 + 16;
    }
    if (y0 < 0) return 0;
    return 0u | ((uint32_t)y0 << 8) | (239u << 16) | ((uint32_t)(y1 - y0 - 1) << 24);
}
