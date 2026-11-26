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

/* temi: DARK (quello originale) e 70S (come il logo: crema, marrone, ruggine, arancio, senape) */
typedef struct { uint16_t back, panel, text, accent, dim, green, amber, red, off, knob[4], stripe[4], led, led_off; } ui_theme;
#define C565(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
static const ui_theme themes[3] = {
    { 0x0000, 0x18e3, 0xef5d, 0x07ff, 0x738e, 0x07e0, 0xfd20, 0xf800, 0x2104,
      { 0x3c9f, 0x07bf, 0x4eae, 0x06d4 }, { 0, 0, 0, 0 }, 0, 0 },
    { C565(246, 234, 206), C565(228, 210, 172), C565(70, 36, 20), C565(196, 98, 28), C565(150, 118, 86),
      C565(96, 118, 30), C565(178, 70, 28), C565(176, 36, 24), C565(206, 188, 150),
      { C565(96, 52, 28), C565(178, 70, 28), C565(214, 118, 32), C565(196, 146, 36) },
      { C565(96, 52, 28), C565(184, 76, 32), C565(232, 136, 40), C565(236, 190, 70) }, 0, 0 },
    /* TWIST: pannello nero e display a LED a matrice di punti, come i 4 caratteri della TWISTfm */
    { C565(8, 8, 10), C565(28, 28, 32), C565(225, 225, 225), C565(255, 90, 40), C565(120, 120, 125),
      C565(255, 150, 40), C565(255, 60, 30), C565(255, 30, 20), C565(40, 40, 44),
      { C565(255, 60, 30), C565(255, 120, 40), C565(255, 170, 60), C565(255, 215, 120) }, { 0, 0, 0, 0 },
      C565(255, 45, 25), C565(48, 10, 8) } };
static const ui_theme *th = &themes[0];
#define BACK (th->back)
#define PANEL (th->panel)
#define TEXT (th->text)
#define ACCENT (th->accent)
#define DIM (th->dim)
#define GREEN (th->green)
#define AMBER (th->amber)
#define RED (th->red)
#define knob_color (th->knob)

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

enum { S_HOME, S_EDIT, S_ENV, S_FILTER, S_GLO, S_INFO, S_FX, S_LFO, S_ARP, S_SEQ, S_SAVE, S_COUNT };
enum { T_F, T_WAVE, T_MULT, T_LIST, T_TUNE, T_NONE, T_VOICES, T_MS, T_AMT, T_BPM, T_LEN, T_STEP };

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
static const char *const L_MODE_LONG[TFM_VOICE_MODES] = { "POLYPHONY", "UNISON", "4 NOTES X 2", "2 NOTES X 4", "MONO L+R", "WIDE PINGPONG", "ROUND ROBIN" };
static const char *const L_VOICES[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
static const char *const L_PRIO[3] = { "LAST", "HIGH", "LOW" };
static const char *const L_DIST[TFM_DIST_SHAPES] = { "TUBE", "FUZZ", "DIOD", "FOLD", "RECT", "CRSH", "HARD", "SINE", "SPLT", "FDBK" };
static const char *const L_DIST_LONG[TFM_DIST_SHAPES] = { "TUBE", "FUZZ", "DIODE", "WAVEFOLD", "RECTIFIER", "BITCRUSH", "HARD CLIP", "SINE FOLD", "SPLIT", "FEEDBACK" };
static const char *const L_REV[TFM_REV_TYPES] = { "ROOM", "PLAT", "SHIM" };
static const char *const L_REV_LONG[TFM_REV_TYPES] = { "ROOM", "PLATE", "LONG SHIMMER" };
static const uint8_t fx_mix_id[4] = { TFM_P_DIST_MIX, TFM_P_PH_MIX, TFM_P_DLY_MIX, TFM_P_REV_MIX };
static const char *const L_SLOT[5] = { "--", "DIST", "PHAS", "TAPE", "REV" };
static const char *const L_LSHAPE[TFM_LFO_SHAPES] = { "TRI", "SAW", "RAMP", "RECT", "NOIZ", "SINE" };
static const char *const L_LMODE[TFM_LFO_MODES] = { "BIPO", "UNI+", "UNI-", "REC+", "REC-" };
static const char *const L_VWAVE[5] = { "SINE", "RECT", "SAW", "RAMP", "NOIZ" };
static const char *const L_VMODE[3] = { "BIPO", "UP", "DOWN" };
static const char *const L_GLMODE[2] = { "ALWS", "LEGT" };
/* comandi che non sono parametri del preset */
enum { V_STEP = 0x400, V_NOTE, V_GATE, V_CLEAR, V_SLOT, V_CH1, V_CH2, V_CH3, V_CH4,
       V_INCH, V_OUTCH, V_NOTEOUT, V_THRU, V_SYNC, V_CLKOUT, V_DTIME, V_THEME,
       V_LFORATE, V_LFORUN = V_LFORATE + 3 };
static const char *const L_BEND[25] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
                                        "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24" };
static const char *const L_RANGE[6] = { "1", "2", "3", "4", "5", "6" };
static const char *const L_SEQNOTE[2] = { "ABS", "RELA" };
static const char *const L_SEQDIR[4] = { "FWD", "REV", "PING", "RAND" };
static const char *const L_ROOT[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const L_GATE[3] = { "REST", "TRIG", "TIE" };
static const char *const L_TEN[11] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };

#define OPID(f) (0x100u | (f)) /* parametro dell'operatore selezionato */
#define FB_ID 0x200u            /* feedback dell'operatore selezionato */
static const pdesc P_HOME[4] = { {T_F, TFM_P_CUTOFF, "CUTOFF", 0, 0}, {T_F, TFM_P_RES, "RES", 0, 0}, {T_F, TFM_P_FLT_ENV, "ENV", 0, 0}, {T_F, TFM_P_HEAT, "HEAT", 0, 0} };
static const pdesc P_EDIT[2][4] = {
    { {T_F, OPID(TFM_OPP_VOL), "VOL", 0, 0}, {T_WAVE, OPID(TFM_OPP_WAVE), "WAVE", L_WAVE, 8}, {T_MULT, OPID(TFM_OPP_MULT), "MULT", L_MULT, 16}, {T_LIST, OPID(TFM_OPP_KVS), "VELO", L_TEN, 11} },
    { {T_LIST, OPID(TFM_OPP_KSL), "KSL", L_KSL, 4}, {T_LIST, OPID(TFM_OPP_KSR), "KSR", L_OFFON, 2}, {T_F, FB_ID, "FB", 0, 0}, {T_NONE, 0, "", 0, 0} } };
static const pdesc P_ENV[4] = { {T_F, OPID(TFM_OPP_A), "ATK", 0, 0}, {T_F, OPID(TFM_OPP_D), "DEC", 0, 0}, {T_F, OPID(TFM_OPP_S), "SUS", 0, 0}, {T_F, OPID(TFM_OPP_R), "REL", 0, 0} };
static const pdesc P_FILTER[3][4] = {
    { {T_F, TFM_P_CUTOFF, "CUTOFF", 0, 0}, {T_F, TFM_P_RES, "RES", 0, 0}, {T_F, TFM_P_FLT_ENV, "ENV", 0, 0}, {T_LIST, TFM_P_FLT_TYPE, "TYPE", L_FT, 15} },
    { {T_F, TFM_P_HEAT, "HEAT", 0, 0}, {T_LIST, TFM_P_FLAVOUR, "FLAV", L_FLAV, 2}, {T_F, TFM_P_WOBBLE, "WOBBLE", 0, 0}, {T_LIST, TFM_P_KEYTRACK, "KTRACK", L_TEN, 11} },
    { {T_F, TFM_P_FLT_A, "ATK", 0, 0}, {T_F, TFM_P_FLT_D, "DEC", 0, 0}, {T_F, TFM_P_FLT_S, "SUS", 0, 0}, {T_F, TFM_P_FLT_R, "REL", 0, 0} } };
static const pdesc P_GLO[11][4] = {
    { {T_LIST, TFM_P_VOICE_MODE, "MODE", L_MODE, TFM_VOICE_MODES}, {T_VOICES, TFM_P_VOICES, "VOICES", L_VOICES, 8}, {T_LIST, TFM_P_PRIO, "PRIO", L_PRIO, 3}, {T_F, TFM_P_FAT, "FAT", 0, 0} },
    { {T_F, TFM_P_FB1, "FB 1", 0, 0}, {T_LIST, TFM_P_ALGO2, "LAYER2", L_L2, 2}, {T_F, TFM_P_FB2, "FB 5", 0, 0}, {T_TUNE, TFM_P_L2_TUNE, "L2TUNE", 0, 49} },
    { {T_F, TFM_P_VOLUME, "VOLUME", 0, 0}, {T_NONE, 0, "", 0, 0}, {T_NONE, 0, "", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_F, TFM_P_VIB_RATE, "RATE", 0, 0}, {T_F, TFM_P_VIB_DEPTH, "DEPTH", 0, 0}, {T_LIST, TFM_P_VIB_WAVE, "WAVE", L_VWAVE, 5}, {T_LIST, TFM_P_VIB_MODE, "MODE", L_VMODE, 3} },
    { {T_F, TFM_P_TREM_RATE, "RATE", 0, 0}, {T_F, TFM_P_TREM_DEPTH, "DEPTH", 0, 0}, {T_F, TFM_P_TREM_PHASE, "PHASE", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_LIST, TFM_P_PORTA, "PORTA", L_OFFON, 2}, {T_F, TFM_P_GLIDE, "TIME", 0, 0}, {T_LIST, TFM_P_GLIDE_MODE, "MODE", L_GLMODE, 2},
      {T_LIST, TFM_P_BEND, "BEND", L_BEND, 25} },
    { {T_F, TFM_P_RISE, "RISE", 0, 0}, {T_F, TFM_P_FALL, "FALL", 0, 0}, {T_NONE, 0, "", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_LIST, TFM_P_SCALE, "SCALE", tfm_scale_names, TFM_SCALES}, {T_LIST, TFM_P_ROOT, "ROOT", L_ROOT, 12},
      {T_LIST, TFM_P_CHORD, "CHORD", tfm_chord_names, TFM_CHORDS}, {T_NONE, 0, "", 0, 0} },
    { {T_STEP, V_INCH, "IN CH", 0, 0}, {T_STEP, V_OUTCH, "OUT CH", 0, 0}, {T_STEP, V_NOTEOUT, "NOTE OUT", 0, 0}, {T_STEP, V_THRU, "THRU", 0, 0} },
    { {T_STEP, V_SYNC, "CLOCK", 0, 0}, {T_STEP, V_CLKOUT, "CLK OUT", 0, 0}, {T_BPM, TFM_P_BPM, "BPM", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_STEP, V_THEME, "THEME", 0, 0}, {T_NONE, 0, "", 0, 0}, {T_NONE, 0, "", 0, 0}, {T_NONE, 0, "", 0, 0} } };
#define GLO_PAGES 11
enum { GP_PORTA = 5, GP_RISE, GP_SCALE, GP_MIDI, GP_SYNC, GP_DISPLAY };
/* GLOBAL a gruppi, come FX: il tasto GLO cambia gruppo, SELECT le pagine del gruppo */
static const uint8_t glo_first[6] = { 0, 3, 5, 7, 8, 10 };   /* VOICES, MOTION, PITCH, SCALE, MIDI, DISPLAY */
static const uint8_t glo_size[6] = { 3, 2, 2, 1, 2, 1 };
static unsigned glo_group(unsigned pg) { unsigned g = 0; while (g < 5 && pg >= glo_first[g + 1]) g++; return g; }
static const pdesc P_FX[7][4] = {
    { {T_LIST, TFM_P_DIST_TYPE, "TYPE", L_DIST, TFM_DIST_SHAPES}, {T_F, TFM_P_DIST_DRIVE, "DRIVE", 0, 0}, {T_F, TFM_P_DIST_TEMP, "TEMP", 0, 0}, {T_F, TFM_P_DIST_MIX, "MIX", 0, 0} },
    { {T_F, TFM_P_PH_RATE, "RATE", 0, 0}, {T_F, TFM_P_PH_DEPTH, "DEPTH", 0, 0}, {T_F, TFM_P_PH_FB, "FDBK", 0, 0}, {T_F, TFM_P_PH_MIX, "MIX", 0, 0} },
    { {T_STEP, V_DTIME, "TIME", 0, 0}, {T_F, TFM_P_DLY_FB, "FDBK", 0, 0}, {T_F, TFM_P_DLY_TONE, "TONE", 0, 0}, {T_F, TFM_P_DLY_MIX, "MIX", 0, 0} },
    { {T_F, TFM_P_DLY_WOW, "WOW", 0, 0}, {T_F, TFM_P_DLY_SAT, "SAT", 0, 0}, {T_F, TFM_P_DLY_SPREAD, "SPREAD", 0, 0}, {T_LIST, TFM_P_DLY_SYNC, "SYNC", L_OFFON, 2} },
    { {T_LIST, TFM_P_REV_TYPE, "TYPE", L_REV, TFM_REV_TYPES}, {T_F, TFM_P_REV_DECAY, "DECAY", 0, 0}, {T_F, TFM_P_REV_TONE, "TONE", 0, 0}, {T_F, TFM_P_REV_MIX, "MIX", 0, 0} },
    { {T_F, TFM_P_REV_PREDLY, "PREDLY", 0, 0}, {T_F, TFM_P_REV_MOD, "MOD", 0, 0}, {T_F, TFM_P_REV_SHIM, "SHIMMR", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_LIST, TFM_P_FX_SLOT1, "SLOT 1", L_SLOT, 5}, {T_LIST, TFM_P_FX_SLOT2, "SLOT 2", L_SLOT, 5}, {T_LIST, TFM_P_FX_SLOT3, "SLOT 3", L_SLOT, 5}, {T_LIST, TFM_P_FX_SLOT4, "SLOT 4", L_SLOT, 5} } };
/* LFO: per ognuno dei 3 LFO una pagina principale e due di chain (4 collegamenti), poi l'inviluppo di modulazione */
#define LMAIN(k) { {T_STEP, V_LFORATE + (k), "RATE", 0, 0}, {T_LIST, TFM_LFO_BASE(k) + 1, "SHAPE", L_LSHAPE, TFM_LFO_SHAPES}, \
                   {T_LIST, TFM_LFO_BASE(k) + 2, "MODE", L_LMODE, TFM_LFO_MODES}, {T_STEP, V_LFORUN + (k), "RUN", 0, 0} }
#define LCH(base, pg, la, lb) { {T_LIST, (base) + 4 * (pg), "DEST " la, tfm_mod_target_names, TFM_MOD_TARGETS}, {T_AMT, (base) + 4 * (pg) + 1, "AMT " la, 0, 41}, \
                              {T_LIST, (base) + 4 * (pg) + 2, "DEST " lb, tfm_mod_target_names, TFM_MOD_TARGETS}, {T_AMT, (base) + 4 * (pg) + 3, "AMT " lb, 0, 41} }
static const pdesc P_LFO[12][4] = {
    LMAIN(0), LCH(TFM_P_LFO1_LINKS, 0, "1", "2"), LCH(TFM_P_LFO1_LINKS, 1, "3", "4"),
    LMAIN(1), LCH(TFM_P_LFO2_LINKS, 0, "1", "2"), LCH(TFM_P_LFO2_LINKS, 1, "3", "4"),
    LMAIN(2), LCH(TFM_P_LFO3_LINKS, 0, "1", "2"), LCH(TFM_P_LFO3_LINKS, 1, "3", "4"),
    { {T_F, TFM_P_MENV_A, "ATK", 0, 0}, {T_F, TFM_P_MENV_D, "DEC", 0, 0}, {T_F, TFM_P_MENV_S, "SUS", 0, 0}, {T_F, TFM_P_MENV_R, "REL", 0, 0} },
    LCH(TFM_P_MENV_LINKS, 0, "1", "2"), LCH(TFM_P_MENV_LINKS, 1, "3", "4") };
/* fase 5: arpeggiatore, sequencer, salvataggio */
static const pdesc P_ARP[2][4] = {
    { {T_LIST, TFM_P_ARP_ON, "ARP", L_OFFON, 2}, {T_LIST, TFM_P_ARP_MODE, "MODE", tfm_arp_names, TFM_ARP_MODES},
      {T_LIST, TFM_P_ARP_RANGE, "OCTAVE", L_RANGE, 6}, {T_LIST, TFM_P_ARP_RATE, "RATE", tfm_div_names, TFM_DIVS} },
    { {T_F, TFM_P_ARP_GATE, "GATE", 0, 0}, {T_LIST, TFM_P_ARP_LATCH, "LATCH", L_OFFON, 2}, {T_F, TFM_P_SWING, "SWING", 0, 0}, {T_BPM, TFM_P_BPM, "BPM", 0, 0} } };
static const pdesc P_SEQ[3][4] = {
    { {T_LIST, TFM_P_SEQ_NOTE, "NOTE", L_SEQNOTE, 2}, {T_LEN, TFM_P_SEQ_LEN, "STEPS", 0, 0},
      {T_LIST, TFM_P_SEQ_DIR, "DIR", L_SEQDIR, 4}, {T_LIST, TFM_P_SEQ_RATE, "RATE", tfm_div_names, TFM_DIVS} },
    { {T_BPM, TFM_P_BPM, "BPM", 0, 0}, {T_F, TFM_P_SEQ_GATE, "GATE", 0, 0}, {T_F, TFM_P_SWING, "SWING", 0, 0}, {T_NONE, 0, "", 0, 0} },
    { {T_STEP, V_STEP, "STEP", 0, 0}, {T_STEP, V_NOTE, "NOTE", 0, 0}, {T_STEP, V_GATE, "GATE", 0, 0}, {T_STEP, V_CLEAR, "CLEAR", 0, 0} } };
static const pdesc P_SAVE[4] = { {T_STEP, V_CH1, "CHR 1", 0, 0}, {T_STEP, V_CH2, "CHR 2", 0, 0}, {T_STEP, V_CH3, "CHR 3", 0, 0}, {T_STEP, V_CH4, "CHR 4", 0, 0} };
static const uint8_t page_count[S_COUNT] = { 1, 2, 1, 3, GLO_PAGES, 1, 7, 12, 2, 3, 1 };

static const uint8_t black_keys[11] = { 1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25 };

typedef struct { char name[5]; uint8_t used; uint8_t p[TFM_P_COUNT]; tfm_seq seq; } user_preset;
#define SAVE_CHARS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+."

struct tfm_ui {
    tfm_patch patch;
    tfm_seq seq;              /* i passi del sequencer del preset corrente */
    user_preset user[TFM_USER_SLOTS];
    uint8_t playing, rec, cursor, play_step, arp_busy, clear_acc, saved_msg;
    uint8_t save_slot;
    char save_name[5];
    tfm_settings set;          /* MIDI e clock: globali, non nel preset */
    uint8_t play_hold;
    uint16_t bpm_now;          /* BPM in uso (con il clock esterno: misurato) */
    uint8_t cpu;               /* carico audio in %, 0 = sconosciuto */
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
const tfm_seq *tfm_ui_seq(const tfm_ui *u) { return &u->seq; }
const tfm_settings *tfm_ui_settings(const tfm_ui *u) { return &u->set; }
void tfm_ui_set_cpu(tfm_ui *u, unsigned pct)
{
    if (pct > 199) pct = 199;
    int d = (int)pct - (int)u->cpu;
    if (d > 2 || d < -2) { u->cpu = (uint8_t)pct; if (u->section == S_HOME) u->dirty = 1; }
}
void tfm_ui_set_bpm(tfm_ui *u, unsigned bpm)
{
    if (bpm != u->bpm_now) { u->bpm_now = (uint16_t)bpm; if (u->section == S_GLO && u->page[S_GLO] == GP_SYNC) u->dirty = 1; }
}
void tfm_ui_settings_load(tfm_ui *u, const tfm_settings *s)
{
    if (s->magic != TFM_SETTINGS_MAGIC) return;
    u->set = *s;
    if (u->set.in_ch > 16) u->set.in_ch = 0;
    if (u->set.out_ch > 15) u->set.out_ch = 0;
    u->set.note_out &= 1; u->set.thru &= 1; u->set.sync &= 1; u->set.clk_out &= 1; if (u->set.theme > 2) u->set.theme = 0;
    tfm_memset(u->band_hash, 0xff, sizeof u->band_hash);
    u->dirty = 1;
}
int tfm_ui_playing(const tfm_ui *u) { return u->playing; }
void tfm_ui_set_play_state(tfm_ui *u, unsigned st)
{
    uint8_t step = (uint8_t)(st & 0xff), busy = (uint8_t)((st >> 9) & 1), run = (uint8_t)((st >> 8) & 1);
    if (u->play_hold) u->play_hold--;
    else if (run != u->playing) { u->playing = run; u->dirty = 1; }   /* START/STOP dal clock MIDI esterno */
    if ((st >> 8 & 1) && step != u->play_step) { u->play_step = step; if (u->section == S_SEQ || u->section == S_HOME) u->dirty = 1; }
    if (busy != u->arp_busy) { u->arp_busy = busy; if (u->section == S_ARP) u->dirty = 1; }
}
/* preset utente in flash: "TWR1", numero di parametri (2 byte), nome (4), parametri, note e gate dei passi */
unsigned tfm_ui_user_blob(const tfm_ui *u, unsigned slot, uint8_t *buf, unsigned max)
{
    unsigned need = 10u + TFM_P_COUNT + 2u * TFM_SEQ_STEPS;
    if (slot >= TFM_USER_SLOTS || !u->user[slot].used || max < need) return 0;
    const user_preset *s = &u->user[slot];
    buf[0] = 'T'; buf[1] = 'W'; buf[2] = 'R'; buf[3] = '1';
    buf[4] = (uint8_t)(TFM_P_COUNT & 0xff); buf[5] = (uint8_t)(TFM_P_COUNT >> 8);
    for (unsigned i = 0; i < 4; i++) buf[6 + i] = (uint8_t)s->name[i];
    tfm_memcpy(buf + 10, s->p, TFM_P_COUNT);
    tfm_memcpy(buf + 10 + TFM_P_COUNT, s->seq.note, TFM_SEQ_STEPS);
    tfm_memcpy(buf + 10 + TFM_P_COUNT + TFM_SEQ_STEPS, s->seq.gate, TFM_SEQ_STEPS);
    return need;
}
int tfm_ui_user_load(tfm_ui *u, unsigned slot, const uint8_t *buf, unsigned len)
{
    if (slot >= TFM_USER_SLOTS || len < 10 || buf[0] != 'T' || buf[1] != 'W' || buf[2] != 'R' || buf[3] != '1') return -1;
    unsigned n = buf[4] | (unsigned)buf[5] << 8;
    if (len < 10u + n + 2u * TFM_SEQ_STEPS) return -1;
    user_preset *s = &u->user[slot];
    tfm_patch def;
    tfm_patch_init(&def);   /* i parametri aggiunti dopo il salvataggio prendono il valore iniziale */
    tfm_memcpy(s->p, def.p, TFM_P_COUNT);
    tfm_memcpy(s->p, buf + 10, n < TFM_P_COUNT ? n : TFM_P_COUNT);
    for (unsigned i = 0; i < TFM_P_COUNT; i++) if (s->p[i] > 127) s->p[i] = 127;
    for (unsigned i = 0; i < 4; i++) { char c = (char)buf[6 + i]; s->name[i] = c >= ' ' && c < 127 ? c : ' '; }
    s->name[4] = 0;
    tfm_memcpy(s->seq.note, buf + 10 + n, TFM_SEQ_STEPS);
    tfm_memcpy(s->seq.gate, buf + 10 + n + TFM_SEQ_STEPS, TFM_SEQ_STEPS);
    for (unsigned i = 0; i < TFM_SEQ_STEPS; i++) { if (s->seq.note[i] > 127) s->seq.note[i] = 60; if (s->seq.gate[i] > 2) s->seq.gate[i] = 0; }
    s->used = 1;
    u->dirty = 1;
    return 0;
}
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

static int preset_used(const tfm_ui *u, unsigned i)
{
    return i < TFM_PRESET_COUNT || (i < TFM_PRESET_COUNT + TFM_USER_SLOTS && u->user[i - TFM_PRESET_COUNT].used);
}
static void load_preset(tfm_ui *u, unsigned i)
{
    if (i < TFM_PRESET_COUNT) {
        for (unsigned k = 0; k < TFM_P_COUNT; k++) u->patch.p[k] = tfm_presets[i].p[k];
        tfm_seq_default(&u->seq);
        if (tfm_presets[i].seq && tfm_presets[i].seq <= TFM_PATTERN_COUNT) {   /* la sequenza del preset */
            const tfm_pattern *pt = &tfm_patterns[tfm_presets[i].seq - 1];
            for (unsigned k = 0; k < 16; k++) { u->seq.note[k] = pt->note[k]; u->seq.gate[k] = pt->gate[k]; }
        }
    } else {
        const user_preset *s = &u->user[i - TFM_PRESET_COUNT];
        tfm_memcpy(u->patch.p, s->p, TFM_P_COUNT);
        u->seq = s->seq;
    }
    if (u->playing && u->patch.p[TFM_P_ARP_ON]) u->patch.p[TFM_P_ARP_ON] = 0;   /* il sequencer in corsa continua */
    u->preset = (uint8_t)i;
    u->saved_msg = 0;
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
    for (unsigned i = 0; i < TFM_USER_SLOTS; i++) tfm_strcpy(u->user[i].name, "----");
    u->set.magic = TFM_SETTINGS_MAGIC;
    u->set.note_out = 1;
    u->set.theme = 2;   /* si parte col tema TWIST (display a LED) */
    load_preset(u, TFM_PRESET_COUNT > 1 ? 1 : 0);
    u->section = S_HOME;
    u->dirty = 1;
    return u;
}

/* ---- parametri ---- */
static void fx_slots(const tfm_ui *u, uint8_t out[4]);
static const char *preset_name(const tfm_ui *u)
{
    return u->preset < TFM_PRESET_COUNT ? tfm_presets[u->preset].name : u->user[u->preset - TFM_PRESET_COUNT].name;
}
static unsigned algo_cur(const tfm_ui *u) { unsigned a = u->patch.p[TFM_P_ALGO]; return a < TFM_ALGOS ? a : TFM_ALGOS - 1; }
static unsigned mode_cur(const tfm_ui *u) { unsigned m = u->patch.p[TFM_P_VOICE_MODE]; return m < TFM_VOICE_MODES ? m : 0; }
static int mode_is_poly(const tfm_ui *u) { unsigned m = mode_cur(u); return m == 0 || m == 5 || m == 6; }
static unsigned voices_cur(const tfm_ui *u) { unsigned v = u->patch.p[TFM_P_VOICES]; return v < 1 || v > 8 ? 8 : v; }
static int mode_is_mono(const tfm_ui *u) { unsigned m = mode_cur(u); return m == 1 || m == 4 || (mode_is_poly(u) && voices_cur(u) == 1); }
static int resolve_id(const tfm_ui *u, const pdesc *d)
{
    if (d->type == T_NONE) return -1;
    if (d->type == T_STEP) return (int)d->id;   /* comando virtuale: >= TFM_P_COUNT */
    if (d->id == FB_ID) /* feedback: solo OP1 e OP5 */
        return u->op == 0 ? TFM_P_FB1 : u->op == 4 ? TFM_P_FB2 : -1;
    if (d->id & 0x100) return (int)TFM_OP_BASE(u->op) + (d->id & 0xff);
    if (d->id == TFM_P_VOICES && !mode_is_poly(u) && mode_cur(u) != 1) return -1;  /* modi polifonici e UNIS */
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
    case S_ARP: return P_ARP[u->page[S_ARP]];
    case S_SEQ: return P_SEQ[u->page[S_SEQ]];
    case S_SAVE: return P_SAVE;
    default: return 0;
    }
}
static void stop_seq(tfm_ui *u)
{
    if (!u->playing) return;
    u->playing = 0;
    u->play_hold = 8;
    push(u, TFM_EV_PLAY, 0, 0);
    u->dirty = 1;
}
static void set_param(tfm_ui *u, int id, int v)
{
    if (id < 0 || id >= TFM_P_COUNT) return;
    if (id == TFM_P_ARP_ON && v) stop_seq(u);   /* arp e sequencer non suonano insieme */
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    if (u->patch.p[id] == v) return;
    u->patch.p[id] = (uint8_t)v;
    push(u, TFM_EV_PARAM, (uint8_t)id, (uint8_t)v);
    u->dirty = 1;
}
static unsigned seq_len_ui(const tfm_ui *u) { return (unsigned)(u->patch.p[TFM_P_SEQ_LEN] & 127) + 1; }
static void write_step(tfm_ui *u, unsigned st, int note, int gate)
{
    if (st >= TFM_SEQ_STEPS) return;
    u->seq.note[st] = (uint8_t)(note < 0 ? 0 : note > 127 ? 127 : note);
    u->seq.gate[st] = (uint8_t)gate;
    push(u, TFM_EV_STEP, (uint8_t)st, 0);
    u->dirty = 1;
}
static void turn_virtual(tfm_ui *u, unsigned id, int delta)
{
    unsigned c = u->cursor;
    switch (id) {
    case V_STEP: {
        int n = (int)seq_len_ui(u), s = ((int)c + delta) % n;
        u->cursor = (uint8_t)(s < 0 ? s + n : s);
        break;
    }
    case V_NOTE: write_step(u, c, (int)u->seq.note[c] + delta, u->seq.gate[c] == TFM_GATE_REST ? TFM_GATE_TRIG : u->seq.gate[c]); break;
    case V_GATE: { int g = (int)u->seq.gate[c] + (delta > 0 ? 1 : -1); write_step(u, c, u->seq.note[c], g < 0 ? 2 : g > 2 ? 0 : g); } break;
    case V_CLEAR:   /* otto scatti in senso orario per cancellare: niente incidenti */
        if (delta < 0) { u->clear_acc = 0; break; }
        u->clear_acc = (uint8_t)(u->clear_acc + delta);
        if (u->clear_acc >= 8) {
            u->clear_acc = 0;
            for (unsigned i = 0; i < TFM_SEQ_STEPS; i++) { u->seq.note[i] = 60; u->seq.gate[i] = TFM_GATE_REST; }
            u->cursor = 0;
            push(u, TFM_EV_SEQ, 0, 0);
        }
        break;
    case V_INCH: { int c = (int)u->set.in_ch + (delta > 0 ? 1 : -1); u->set.in_ch = (uint8_t)(c < 0 ? 0 : c > 16 ? 16 : c); push(u, TFM_EV_SETTINGS, 0, 0); } break;
    case V_OUTCH: { int c = (int)u->set.out_ch + (delta > 0 ? 1 : -1); u->set.out_ch = (uint8_t)(c < 0 ? 0 : c > 15 ? 15 : c); push(u, TFM_EV_SETTINGS, 0, 0); } break;
    case V_NOTEOUT: u->set.note_out = delta > 0; push(u, TFM_EV_SETTINGS, 0, 0); break;
    case V_THRU: u->set.thru = delta > 0; push(u, TFM_EV_SETTINGS, 0, 0); break;
    case V_SYNC: u->set.sync = delta > 0; push(u, TFM_EV_SETTINGS, 0, 0); break;
    case V_CLKOUT: u->set.clk_out = delta > 0; push(u, TFM_EV_SETTINGS, 0, 0); break;
    case V_THEME: {
        int t = (int)u->set.theme + (delta > 0 ? 1 : -1);
        t = t < 0 ? 0 : t > 2 ? 2 : t;
        if (t != u->set.theme) { u->set.theme = (uint8_t)t; tfm_memset(u->band_hash, 0xff, sizeof u->band_hash); push(u, TFM_EV_SETTINGS, 0, 0); }
        break;
    }
    case V_DTIME:   /* eco: millisecondi, oppure divisione se SYNC e' acceso */
        if (u->patch.p[TFM_P_DLY_SYNC]) {
            int d = (int)(u->patch.p[TFM_P_DLY_DIV] % TFM_DLY_DIVS) + (delta > 0 ? 1 : -1);
            set_param(u, TFM_P_DLY_DIV, d < 0 ? 0 : d >= TFM_DLY_DIVS ? TFM_DLY_DIVS - 1 : d);
        } else set_param(u, TFM_P_DLY_TIME, u->patch.p[TFM_P_DLY_TIME] + delta);
        break;
    case V_LFORATE: case V_LFORATE + 1: case V_LFORATE + 2: {   /* Hz liberi, o divisione se a tempo */
        unsigned k = id - V_LFORATE;
        if (u->patch.p[TFM_P_LFO1_SYNC + k]) {
            int d = (int)(u->patch.p[TFM_P_LFO1_DIV + k] % TFM_LFO_DIVS) + (delta > 0 ? 1 : -1);
            set_param(u, (int)(TFM_P_LFO1_DIV + k), d < 0 ? 0 : d >= TFM_LFO_DIVS ? TFM_LFO_DIVS - 1 : d);
        } else set_param(u, (int)TFM_LFO_BASE(k), u->patch.p[TFM_LFO_BASE(k)] + delta);
        break;
    }
    case V_LFORUN: case V_LFORUN + 1: case V_LFORUN + 2: {   /* FREE, RETRIG, SYNC, SYNC + RETRIG */
        unsigned k = id - V_LFORUN;
        int r = (u->patch.p[TFM_LFO_BASE(k) + 3] ? 1 : 0) + (u->patch.p[TFM_P_LFO1_SYNC + k] ? 2 : 0) + (delta > 0 ? 1 : -1);
        r = r < 0 ? 0 : r > 3 ? 3 : r;
        set_param(u, (int)(TFM_LFO_BASE(k) + 3), r & 1);
        set_param(u, (int)(TFM_P_LFO1_SYNC + k), r >> 1);
        break;
    }
    case V_CH1: case V_CH2: case V_CH3: case V_CH4: {
        static const char set[] = SAVE_CHARS;
        int n = (int)sizeof set - 1, k = (int)(id - V_CH1), cur = 0;
        for (int i = 0; i < n; i++) if (set[i] == u->save_name[k]) cur = i;
        cur = ((cur + (delta > 0 ? 1 : -1)) % n + n) % n;
        u->save_name[k] = set[cur];
        break;
    }
    default: break;
    }
    u->dirty = 1;
}
static void turn_param(tfm_ui *u, const pdesc *d, int delta)
{
    int id = resolve_id(u, d);
    if (id < 0) return;
    if (d->type == T_STEP) { turn_virtual(u, (unsigned)id, delta); return; }
    if (d->type == T_BPM || d->type == T_LEN) { set_param(u, id, u->patch.p[id] + delta); return; }
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
static void num_text(char *out, unsigned n)
{
    char tmp[6]; int k = 0;
    do { tmp[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 5);
    for (int i = 0; i < k; i++) out[i] = tmp[k - 1 - i];
    out[k] = 0;
}
/* nome della nota MIDI: C4 = 60 */
static void note_text(char *out, unsigned n)
{
    tfm_strcpy(out, L_ROOT[n % 12]);
    unsigned l = (unsigned)tfm_strlen(out);
    int o = (int)(n / 12) - 1;
    if (o < 0) { out[l++] = '-'; o = 1; }
    out[l] = (char)('0' + o);
    out[l + 1] = 0;
}
static void value_text(const tfm_ui *u, const pdesc *d, char *out)
{
    int id = resolve_id(u, d);
    if (id < 0) { tfm_strcpy(out, d->type == T_NONE ? "" : "-"); return; }
    if (d->type == T_STEP) {
        unsigned c = u->cursor;
        switch (id) {
        case V_STEP: num_text(out, c + 1); break;
        case V_NOTE: if (u->seq.gate[c] == TFM_GATE_REST) tfm_strcpy(out, "--"); else note_text(out, u->seq.note[c]); break;
        case V_GATE: tfm_strcpy(out, L_GATE[u->seq.gate[c] % 3]); break;
        case V_CLEAR: { static const char *const bar[9] = { ">>>", "1/8", "2/8", "3/8", "4/8", "5/8", "6/8", "7/8", "8/8" };
                        tfm_strcpy(out, bar[u->clear_acc > 8 ? 8 : u->clear_acc]); } break;
        case V_INCH: if (u->set.in_ch) num_text(out, u->set.in_ch); else tfm_strcpy(out, "OMNI"); break;
        case V_OUTCH: num_text(out, u->set.out_ch + 1u); break;
        case V_NOTEOUT: tfm_strcpy(out, u->set.note_out ? "ON" : "OFF"); break;
        case V_THRU: tfm_strcpy(out, u->set.thru ? "ON" : "OFF"); break;
        case V_SYNC: tfm_strcpy(out, u->set.sync ? "EXT" : "INT"); break;
        case V_CLKOUT: tfm_strcpy(out, u->set.clk_out ? "ON" : "OFF"); break;
        case V_LFORATE: case V_LFORATE + 1: case V_LFORATE + 2: {
            unsigned k = (unsigned)id - V_LFORATE;
            if (u->patch.p[TFM_P_LFO1_SYNC + k]) tfm_strcpy(out, tfm_lfo_div_names[u->patch.p[TFM_P_LFO1_DIV + k] % TFM_LFO_DIVS]);
            else num_text(out, u->patch.p[TFM_LFO_BASE(k)]);
        } break;
        case V_LFORUN: case V_LFORUN + 1: case V_LFORUN + 2: {
            static const char *const rn[4] = { "FREE", "RTRG", "SYNC", "S+RT" };
            unsigned k = (unsigned)id - V_LFORUN;
            tfm_strcpy(out, rn[(u->patch.p[TFM_LFO_BASE(k) + 3] ? 1 : 0) + (u->patch.p[TFM_P_LFO1_SYNC + k] ? 2 : 0)]);
        } break;
        case V_THEME: { static const char *const tn[3] = { "DARK", "70S", "TWST" }; tfm_strcpy(out, tn[u->set.theme % 3]); } break;
        case V_DTIME:
            if (u->patch.p[TFM_P_DLY_SYNC]) tfm_strcpy(out, tfm_dly_div_names[u->patch.p[TFM_P_DLY_DIV] % TFM_DLY_DIVS]);
            else { float ms = 30.f; for (unsigned k = 0; k < u->patch.p[TFM_P_DLY_TIME]; k++) ms *= 1.02511f; num_text(out, (unsigned)(ms + 0.5f)); }
            break;
        default: { char s[2] = { u->save_name[id - V_CH1], 0 }; tfm_strcpy(out, s[0] == ' ' ? "_" : s); } break;
        }
        return;
    }
    uint8_t v = u->patch.p[id];
    if (d->type == T_BPM) { num_text(out, 60u + v); return; }
    if (d->type == T_LEN) { num_text(out, (v & 127u) + 1u); return; }
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
    if (enc == TFM_ENC_PRESETS) {   /* preset di fabbrica, poi gli slot utente non vuoti */
        int total = TFM_PRESET_COUNT + TFM_USER_SLOTS, p = (int)u->preset;
        do { p = (p + (delta > 0 ? 1 : total - 1)) % total; } while (!preset_used(u, (unsigned)p));
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
        if (u->section == S_SAVE) u->save_slot = (uint8_t)((u->save_slot + (delta > 0 ? 1 : TFM_USER_SLOTS - 1)) % TFM_USER_SLOTS);
        else if (u->section == S_FX) {   /* solo fra le pagine dell'effetto: TAPE <> TAPE+, REVERB <> REVERB+ */
            unsigned pg = u->page[S_FX];
            if (pg == 2 || pg == 3) u->page[S_FX] = (uint8_t)(pg ^ 1u);
            else if (pg == 4 || pg == 5) u->page[S_FX] = (uint8_t)(pg == 4 ? 5 : 4);
        } else if (u->section == S_GLO) {   /* solo dentro il gruppo */
            unsigned g = glo_group(u->page[S_GLO]), n = glo_size[g], i = u->page[S_GLO] - glo_first[g];
            u->page[S_GLO] = (uint8_t)(glo_first[g] + (i + (delta > 0 ? 1 : n - 1)) % n);
        } else if (u->section == S_LFO) {   /* solo fra le 3 pagine dell'LFO (o del MOD ENV) scelto */
            unsigned pg = u->page[S_LFO], g = pg / 3 * 3;
            u->page[S_LFO] = (uint8_t)(g + (pg - g + (delta > 0 ? 1 : 2)) % 3);
        } else if (page_count[u->section] > 1) {
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
    if (b != TFM_BTN_SAVE) u->saved_msg = 0;
    switch (b) {
    case TFM_BTN_OCT_DOWN: if (u->octave > -3) u->octave--; u->dirty = 1; break;
    case TFM_BTN_OCT_UP: if (u->octave < 3) u->octave++; u->dirty = 1; break;
    case TFM_BTN_HOME: goto_section(u, S_HOME, 0); break;
    case TFM_BTN_EDIT:   /* EDIT ed ENV: premuti di nuovo passano all'operatore successivo (le pagine con SELECT) */
    case TFM_BTN_ENV: {
        unsigned s = b == TFM_BTN_EDIT ? S_EDIT : S_ENV;
        if (u->section == s) u->op = (uint8_t)((u->op + 1) % 6);
        u->section = (uint8_t)s;
        u->touched = -1;
        u->dirty = 1;
        break;
    }
    case TFM_BTN_FX: {   /* DIST > PHASER > TAPE > REVERB > CHAIN; le seconde pagine (TAPE+, REVERB+) solo con SELECT */
        static const uint8_t next_main[7] = { 1, 2, 4, 4, 6, 6, 0 };
        if (u->section == S_FX) u->page[S_FX] = next_main[u->page[S_FX] % 7];
        u->section = S_FX; u->touched = -1; u->dirty = 1;
        break;
    }
    case TFM_BTN_SEL: goto_section(u, S_FILTER, 0); break;
    case TFM_BTN_GLO:   /* VOICES > MOTION > PITCH > SCALE > MIDI > DISPLAY */
        if (u->section == S_GLO) u->page[S_GLO] = glo_first[(glo_group(u->page[S_GLO]) + 1) % 6];
        u->section = S_GLO; u->touched = -1; u->dirty = 1;
        break;
    case TFM_BTN_LFO: /* LFO 1 > LFO 2 > LFO 3 > MOD ENV; le pagine di chain con SELECT */
        if (u->section == S_LFO) u->page[S_LFO] = (uint8_t)((u->page[S_LFO] / 3 + 1) % 4 * 3);
        u->section = S_LFO; u->touched = -1; u->dirty = 1;
        break;
    case TFM_BTN_ARP:   /* fuori dalla pagina: la apre; dentro: accende/spegne */
        if (u->section == S_ARP) set_param(u, TFM_P_ARP_ON, u->patch.p[TFM_P_ARP_ON] ? 0 : 1);
        u->section = S_ARP; u->touched = -1; u->dirty = 1;
        break;
    case TFM_BTN_SEQ: goto_section(u, S_SEQ, 0); break;
    case TFM_BTN_PLAY:
        if (u->playing) stop_seq(u);
        else {
            set_param(u, TFM_P_ARP_ON, 0);
            u->playing = 1;
            u->play_hold = 8;
            u->play_step = 0;
            push(u, TFM_EV_PLAY, 1, 0);
        }
        u->dirty = 1;
        break;
    case TFM_BTN_REC:   /* registrazione dei passi dalla tastiera */
        u->rec ^= 1u;
        if (u->rec) { u->section = S_SEQ; u->page[S_SEQ] = 2; u->touched = -1; }
        u->dirty = 1;
        break;
    case TFM_BTN_SAVE:
        if (u->section == S_SAVE) {   /* seconda pressione: salva */
            user_preset *s = &u->user[u->save_slot];
            tfm_memcpy(s->p, u->patch.p, TFM_P_COUNT);
            s->seq = u->seq;
            tfm_memcpy(s->name, u->save_name, 4);
            s->name[4] = 0;
            s->used = 1;
            push(u, TFM_EV_SAVE, u->save_slot, 0);
            u->preset = (uint8_t)(TFM_PRESET_COUNT + u->save_slot);
            u->section = S_HOME;
            u->saved_msg = 1;
        } else {
            if (u->preset >= TFM_PRESET_COUNT) u->save_slot = (uint8_t)(u->preset - TFM_PRESET_COUNT);
            else {   /* il primo slot libero */
                u->save_slot = 0;
                for (unsigned i = 0; i < TFM_USER_SLOTS; i++) if (!u->user[i].used) { u->save_slot = (uint8_t)i; break; }
            }
            const char *n = preset_name(u);
            for (unsigned i = 0; i < 4; i++) {
                char c = n[i] ? n[i] : ' ';
                if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                int ok = 0;
                for (const char *s = SAVE_CHARS; *s; s++) if (*s == c) ok = 1;
                u->save_name[i] = ok ? c : ' ';
                if (!n[i]) n = "    ";
            }
            u->save_name[4] = 0;
            u->section = S_SAVE;
        }
        u->touched = -1;
        u->dirty = 1;
        break;
    default: break;
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
                    else if (i < 8) { u->section = S_GLO; u->page[S_GLO] = i == 6 ? GP_PORTA : 0; }   /* PITCH, GLOBAL */
                    else set_param(u, TFM_P_VOICE_MODE, i == 8 ? 4 : 0);     /* MONO, POLY = POL8 */
                    u->touched = -1;
                    u->dirty = 1;
                    return;
                }
        }
        int note = 53 + (int)key + 12 * u->octave; /* il tasto 0 e' FA3 */
        if (note < 0 || note > 127) return;
        u->key_note[key] = (uint8_t)note;
        if (u->rec || (u->section == S_SEQ && u->page[S_SEQ] == 2)) {   /* scrive il passo */
            unsigned st = u->playing && u->rec ? u->play_step : u->cursor;
            write_step(u, st, note, TFM_GATE_TRIG);
            if (!(u->playing && u->rec)) u->cursor = (uint8_t)((u->cursor + 1) % seq_len_ui(u));
        }
        push(u, TFM_EV_NOTE_ON, (uint8_t)note, (uint8_t)vel);
    } else if (u->key_note[key] != 0xff) {
        push(u, TFM_EV_NOTE_OFF, u->key_note[key], 0);
        u->key_note[key] = 0xff;
    }
}

uint64_t tfm_ui_leds(const tfm_ui *u)
{
    uint64_t l = 0;
    static const uint8_t sec_btn[S_COUNT] = { TFM_BTN_HOME, TFM_BTN_EDIT, TFM_BTN_ENV, TFM_BTN_SEL, TFM_BTN_GLO, 0, TFM_BTN_FX, TFM_BTN_LFO,
                                              TFM_BTN_ARP, TFM_BTN_SEQ, TFM_BTN_SAVE };
    if (u->section == S_INFO) l |= 1ull << u->info;
    else l |= 1ull << sec_btn[u->section];
    if (u->patch.p[TFM_P_ARP_ON]) l |= 1ull << TFM_BTN_ARP;
    if (u->playing) l |= 1ull << TFM_BTN_PLAY;
    if (u->rec) l |= 1ull << TFM_BTN_REC;
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
    static const uint8_t small_m[5] = { 0x7c, 0x04, 0x18, 0x04, 0x78 };   /* "m" minuscola: PNTm, HARm... */
    if (ch == 'm') return small_m;
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

/* display a LED a matrice di punti: 4 caratteri 5x7, punti di 3 pixel */
static void led_text(tfm_ui *u, int x, int y, const char *s)
{
    if (y >= (int)u->band_top + 16 || y + 28 <= (int)u->band_top) return;
    for (int i = 0; i < 4; i++) {
        const uint8_t *g = glyph(*s ? *s : ' ');
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                fill(u, x + i * 24 + col * 4, y + row * 4, 3, 3, (g[col] >> row) & 1u ? th->led : th->led_off);
        if (*s) s++;
    }
}
static void value_text(const tfm_ui *u, const pdesc *d, char *out);
static const pdesc *page_params(const tfm_ui *u);
static void header(tfm_ui *u, const char *title, const char *right)
{
    if (th->led) {   /* TWIST: a sinistra il display a 4 caratteri, come sulla TWISTfm */
        fill(u, 0, 0, 240, 32, BACK);
        fill(u, 2, 0, 100, 31, C565(20, 4, 4));
        char v[12];
        const pdesc *ps = page_params(u);
        if (u->touched >= 0 && ps) value_text(u, &ps[u->touched], v);   /* il valore della manopola girata */
        else { int k = 0; while (title[k] && k < 4) { v[k] = title[k]; k++; } v[k] = 0; }
        led_text(u, 6, 2, v);
        text(u, 110, 6, title, 1, TEXT);
        if (right) text(u, 110, 20, right, 1, ACCENT);
        return;
    }
    fill(u, 0, 0, 240, 28, PANEL);
    if (th->stripe[0]) for (int k = 0; k < 4; k++) fill(u, 0, 24 + k, 240, 1, th->stripe[k]);   /* 70S: le 4 strisce */
    text(u, 6, 5, title, 2, AMBER);
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
    char num[12] = "P";
    num_text(num + 1, u->preset + 1u);   /* P7/128 */
    {
        unsigned l = (unsigned)tfm_strlen(num);
        num[l] = '/';
        num_text(num + l + 1, TFM_PRESET_COUNT);
    }
    if (u->preset >= TFM_PRESET_COUNT) {   /* preset utente */
        unsigned s = u->preset - TFM_PRESET_COUNT + 1u;
        num[0] = 'U'; num[1] = (char)('0' + s / 10); num[2] = (char)('0' + s % 10); num[3] = 0;
    }
    header(u, "HOME", u->saved_msg ? "SAVED!" : num);
    text(u, 6, 40, preset_name(u), 3, TEXT);
    {   /* arp e sequencer, se attivi */
        int sx = 234;
        if (u->playing) { char st[8]; num_text(st, u->play_step + 1u); sx -= text_w(st, 1); text(u, sx, 44, st, 1, GREEN); sx -= text_w("SEQ ", 1) + 2; text(u, sx, 44, "SEQ", 1, GREEN); }
        if (u->patch.p[TFM_P_ARP_ON]) { sx -= text_w("ARP", 1) + 8; text(u, sx, 44, "ARP", 1, AMBER); }
    }
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
    text(u, 6, 152, "FILTER", 1, DIM);
    int x = text(u, 48, 150, L_FT[ft], 1, knob_color[1]);
    text(u, x + 8, 150, L_FLAV[p[TFM_P_FLAVOUR] ? 1 : 0], 1, p[TFM_P_FLAVOUR] ? AMBER : ACCENT);
    text(u, 6, 164, "VOICES", 1, DIM);
    {
        char vm[24];
        unsigned m = mode_cur(u);
        tfm_strcpy(vm, L_MODE[m]);
        if (mode_is_poly(u) || m == 1) { unsigned n = (unsigned)tfm_strlen(vm); vm[n] = ' '; vm[n + 1] = (char)('0' + voices_cur(u)); vm[n + 2] = 'V'; vm[n + 3] = 0; }
        x = text(u, 48, 164, vm, 1, TEXT);
    }
    text(u, x + 12, 164, "OCT", 1, DIM);
    if (u->cpu) {   /* carico del processore audio */
        char cs[10] = "CPU ";
        num_text(cs + 4, u->cpu);
        unsigned l = (unsigned)tfm_strlen(cs);
        cs[l] = '%'; cs[l + 1] = 0;
        text(u, 234 - text_w(cs, 1), 164, cs, 1, u->cpu > 85 ? RED : u->cpu > 65 ? AMBER : DIM);
    }
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
    header(u, "EDIT", (car >> u->op) & 1 ? "CARRIER" : "MODULATOR");
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
    text(u, 6, 140, "PAGE", 1, DIM);
    text(u, 30, 140, u->page[S_EDIT] ? "2 SCALE" : "1 BASIC", 1, TEXT);
    text(u, 6, 164, "EDIT = NEXT OP  SELECT = PAGE", 1, DIM);
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
    text(u, 10, 178, "ENV = NEXT OP (OR BLACK KEYS)", 1, DIM);
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
        if (i) line(u, bx + i, py, bx + 1 + i, yy, knob_color[0]);   /* il colore di CUTOFF (K1) */
        py = yy;
    }
}

static void draw_filter(tfm_ui *u)
{
    static const char *const sub[3] = { "FILTER", "CHARACTER", "ENVELOPE" };
    const uint8_t *p = u->patch.p;
    header(u, "FILTER", p[TFM_P_FLAVOUR] ? "MS20" : "LIQUID");
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
    static const char *const sub[3] = { "MAIN", "CHAIN 1-2", "CHAIN 3-4" };
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
            int syn = p[TFM_P_LFO1_SYNC + k] != 0;
            if (syn) {
                int x = text(u, 6, 140, tfm_lfo_div_names[p[TFM_P_LFO1_DIV + k] % TFM_LFO_DIVS], 2, TEXT);
                text(u, x + 4, 147, "SYNC", 1, GREEN);
            } else {
                fmt_fixed(hz, lfo_rate_hz(q[0]), 2);
                int x = text(u, 6, 140, hz, 2, TEXT);
                text(u, x + 4, 147, "HZ", 1, DIM);
            }
            text(u, 130, 140, q[3] ? "RETRIG" : "FREE", 2, q[3] ? AMBER : DIM);
            /* collegamenti attivi in breve */
            unsigned n = 0;
            for (unsigned i = 0; i < TFM_LINKS; i++)
                if (p[TFM_LFO_LINK(k, i)] && p[TFM_LFO_LINK(k, i) + 1] != TFM_AMT_ZERO) n++;
            char s[16] = "CHAIN: 0 ACTIVE";
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

static void glo_dots(tfm_ui *u);
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
        text(u, x + 4, 147, "DEG L/R", 1, DIM);
    } else if (pg == GP_PORTA) {   /* portamento: da una nota all'altra */
        int bx = 6, by = 36, bw = 228, bh = 92;
        frame(u, bx, by, bw, bh, PANEL);
        int on = p[TFM_P_PORTA] != 0;
        int lo = by + bh - 18, hi = by + 22, x1 = bx + 60;
        line(u, bx + 6, lo, x1, lo, DIM);
        text(u, bx + 8, lo + 6, "NOTE 1", 1, DIM);
        int gw = on ? 4 + (int)((float)p[TFM_P_GLIDE] / 127.f * 140.f) : 1;
        int py = lo;
        for (int i = 0; i <= bx + bw - 6 - x1; i++) {
            float t = (float)i / (float)gw;
            float e = t >= 1.f ? 1.f : 1.f - (1.f - t) * (1.f - t);
            int yy = lo - (int)((float)(lo - hi) * e);
            if (i) line(u, x1 + i - 1, py, x1 + i, yy, on ? knob_color[1] : DIM);
            py = yy;
        }
        text(u, bx + bw - 48, hi - 12, "NOTE 2", 1, DIM);
        int x = text(u, 6, 136, on ? "PORTAMENTO ON" : "PORTAMENTO OFF", 2, on ? AMBER : DIM);
        text(u, 6, 155, p[TFM_P_GLIDE_MODE] ? "LEGATO: ONLY WHEN NOTES OVERLAP" : "ALWAYS: EVERY NOTE GLIDES", 1, on ? TEXT : DIM);
        num_text(s, p[TFM_P_BEND] < 1 ? 1u : p[TFM_P_BEND]);
        x = text(u, 6, 168, "PITCH BEND +/-", 1, DIM);
        x = text(u, x + 6, 168, s, 1, TEXT);
        text(u, x + 6, 168, "SEMITONES", 1, DIM);
    } else {   /* rise e fall: il pitch sale all'attacco, cade al rilascio */
        int bx = 6, by = 36, bw = 228, bh = 92;
        frame(u, bx, by, bw, bh, PANEL);
        int mid = by + bh / 2, xr = bx + 150;
        float rise = (float)p[TFM_P_RISE] / 127.f, fall = (float)p[TFM_P_FALL] / 127.f;
        fill(u, bx + 2, mid, bw - 4, 1, PANEL);
        int py = mid + (int)(rise * 36.f);
        for (int i = 1; i < xr - bx - 4; i++) {
            float r = (float)i / 22.f;
            int yy = mid + (int)(rise * 36.f * (r > 3.f ? 0.f : (1.f - r / 3.f) * (1.f - r / 3.f)));
            line(u, bx + 3 + i, py, bx + 4 + i, yy, knob_color[0]);
            py = yy;
        }
        for (int i = 1; i < bx + bw - 4 - xr; i++) {
            float f = (float)i / (8.f + fall * 30.f);
            int yy = mid + (int)(fall * 40.f * (1.f - 1.f / (1.f + f)));
            if (yy > by + bh - 3) yy = by + bh - 3;
            line(u, xr + i - 1, py, xr + i, yy, knob_color[1]);
            py = yy;
        }
        fill(u, xr, by + 2, 1, bh - 4, PANEL);
        text(u, bx + 6, by + 6, "NOTE ON", 1, DIM);
        text(u, xr + 4, by + 6, "NOTE OFF", 1, DIM);
        text(u, 6, 140, "RISE: SCOOP UP AT START", 1, p[TFM_P_RISE] ? TEXT : DIM);
        text(u, 6, 154, "FALL: DROP AFTER RELEASE", 1, p[TFM_P_FALL] ? TEXT : DIM);
    }
    glo_dots(u);
    footer(u, P_GLO[pg]);
}

static void draw_scale(tfm_ui *u);
static void glo_dots(tfm_ui *u)
{
    unsigned g = glo_group(u->page[S_GLO]);
    page_dots(u, glo_size[g], u->page[S_GLO] - glo_first[g]);
}
static void draw_midi(tfm_ui *u, unsigned pg)
{
    const tfm_settings *s = &u->set;
    if (pg == GP_MIDI) {
        text(u, 6, 38, "INPUT", 2, AMBER);
        text(u, 6, 58, "USB + TRS JACK (3.5MM)", 1, TEXT);
        char c[24] = "CHANNEL: ";
        if (s->in_ch) num_text(c + 9, s->in_ch); else tfm_strcpy(c + 9, "OMNI (ALL)");
        text(u, 6, 70, c, 1, TEXT);
        text(u, 6, 82, s->thru ? "THRU: IN -> USB OUT" : "THRU: OFF", 1, s->thru ? GREEN : DIM);
        text(u, 6, 102, "OUTPUT", 2, AMBER);
        text(u, 6, 122, "USB ONLY (NO MIDI OUT JACK)", 1, TEXT);
        tfm_strcpy(c, "CHANNEL: ");
        num_text(c + 9, s->out_ch + 1u);
        text(u, 6, 134, c, 1, TEXT);
        text(u, 6, 146, s->note_out ? "NOTES: KEYS -> USB" : "NOTES: OFF", 1, s->note_out ? GREEN : DIM);
        text(u, 6, 166, "PITCH BEND RANGE: GLO > PITCH", 1, DIM);
    } else {
        text(u, 6, 38, s->sync ? "EXTERNAL CLOCK" : "INTERNAL CLOCK", 2, AMBER);
        if (s->sync) {
            text(u, 6, 60, "ARP, SEQ AND SYNCED ECHO", 1, TEXT);
            text(u, 6, 72, "FOLLOW MIDI CLOCK (USB/TRS)", 1, TEXT);
            text(u, 6, 84, "START/STOP CONTROL THE SEQ", 1, TEXT);
        } else {
            text(u, 6, 60, "TEMPO FROM THE BPM KNOB", 1, TEXT);
            text(u, 6, 72, "(SAVED WITH THE PRESET)", 1, DIM);
        }
        char b[16];
        num_text(b, u->bpm_now ? u->bpm_now : 60u + u->patch.p[TFM_P_BPM]);
        int x = text(u, 6, 108, b, 4, TEXT);
        text(u, x + 6, 122, "BPM", 2, DIM);
        if (s->clk_out && s->sync) text(u, 6, 150, "CLOCK OUT: ONLY WITH INT CLOCK", 1, DIM);
        else {
            text(u, 6, 150, s->clk_out ? "CLOCK OUT: 24 PPQN -> USB" : "CLOCK OUT: OFF", 1, s->clk_out ? GREEN : DIM);
            if (s->clk_out) text(u, 6, 162, "WITH START/STOP ON PLAY", 1, DIM);
        }
    }
    glo_dots(u);
    footer(u, P_GLO[pg]);
}
static void draw_glo(tfm_ui *u)
{
    static const char *const sub[GLO_PAGES] = { "VOICES", "LAYER", "LEVEL", "VIBRATO", "TREMOLO", "PORTAMENTO",
                                                "RISE/FALL", "SCALE", "MIDI", "SYNC", "DISPLAY" };
    header(u, "GLOBAL", sub[u->page[S_GLO]]);
    if (u->page[S_GLO] == GP_SCALE) { draw_scale(u); glo_dots(u); footer(u, P_GLO[GP_SCALE]); return; }
    if (u->page[S_GLO] == GP_MIDI || u->page[S_GLO] == GP_SYNC) { draw_midi(u, u->page[S_GLO]); return; }
    if (u->page[S_GLO] == GP_DISPLAY) {   /* tema dello schermo */
        text(u, 6, 38, "THEME", 2, AMBER);
        { static const char *const td[3] = { "DARK: BLACK, BEST ON STAGE", "70S: CREAM, BROWN, RUST, MUSTARD", "TWIST: LED DISPLAY LIKE THE TWISTFM" };
          text(u, 6, 60, td[u->set.theme % 3], 1, TEXT); }
        for (int k = 0; k < 4; k++) fill(u, 6 + k * 57, 84, 52, 40, knob_color[k]);
        if (th->stripe[0]) for (int k = 0; k < 4; k++) fill(u, 6, 134 + k * 6, 228, 4, th->stripe[k]);
        text(u, 6, 166, "SAVED ON THE FM-1, NOT IN PRESETS", 1, DIM);
        glo_dots(u);
        footer(u, P_GLO[GP_DISPLAY]);
        return;
    }
    if (u->page[S_GLO] >= 3) { draw_glo_mod(u, u->page[S_GLO]); return; }
    const uint8_t *p = u->patch.p;
    unsigned m = mode_cur(u);
    /* le 8 voci: quadratini accesi in base al modo e al limite di polifonia */
    text(u, 6, 36, "VOICES", 1, DIM);
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
    if (mode_is_mono(u)) { text(u, 6, 94, "PRIORITY", 1, DIM); text(u, 60, 94, L_PRIO[p[TFM_P_PRIO] > 2 ? 0 : p[TFM_P_PRIO]], 1, AMBER); }
    else { text(u, 6, 94, "MAX NOTES", 1, DIM); char n[2] = { (char)('0' + (m == 2 ? 4 : m == 3 ? 2 : used)), 0 }; text(u, 66, 94, n, 1, AMBER); }
    char tv[8];
    value_text(u, &P_GLO[1][3], tv);
    text(u, 6, 120, "LAYER 2", 1, DIM);
    int x = text(u, 6, 134, p[TFM_P_ALGO2] ? "PARALLEL" : "SERIES", 2, TEXT);
    x = text(u, x + 12, 134, tv, 2, knob_color[3]);
    text(u, x + 4, 141, "ST", 1, DIM);
    glo_dots(u);
    footer(u, P_GLO[u->page[S_GLO]]);
}


/* ---- effetti ---- */
static const char *const fx_page_name[7] = { "DISTORTION", "PHASER", "TAPE ECHO", "TAPE ECHO 2", "REVERB", "REVERB 2", "CHAIN" };
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
            static const char *const longn[5] = { "EMPTY", "DISTORTION", "PHASER", "TAPE ECHO", "REVERB" };
            text(u, 42, y + 6, longn[f], 1, c);
            text(u, 164, y + 6, f ? (on ? "ON" : "MIX 0") : "", 1, on ? GREEN : DIM);
        }
        text(u, 6, 160, "OUTPUT", 1, DIM);
        text(u, 60, 172, "SAME EFFECT = SWAP", 1, DIM);
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
        text(u, bx + 6, by + 6, "6 STAGES STEREO", 1, TEXT);
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
        text(u, bx + 6, by + 6, "1.5 SECONDS", 1, DIM);
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
    text(u, 6, 164, pg >= 2 && pg <= 5 ? "FX = NEXT EFFECT  SELECT = MORE" : "FX = NEXT EFFECT", 1, DIM);
    if (pg >= 2 && pg <= 5) page_dots(u, 2, (pg - 2) & 1u);   /* TAPE e REVERB: due pagine */
    footer(u, P_FX[pg]);
}

/* ---- fase 5: arpeggiatore, sequencer, salvataggio ---- */
/* le note dell'arpeggio per il disegno: tasti tenuti (o DO-MI-SOL), come fa il motore */
static unsigned arp_preview(const tfm_ui *u, uint8_t *out, unsigned steps, uint8_t *lo, uint8_t *hi)
{
    uint8_t s[16], h[16];
    unsigned n = 0;
    for (unsigned k = 0; k < TFM_KEYS && n < 16; k++) if (u->key_note[k] != 0xff) h[n++] = u->key_note[k];
    if (!n) { h[0] = 60; h[1] = 64; h[2] = 67; n = 3; }
    for (unsigned i = 0; i < n; i++) { unsigned j = i; while (j && s[j - 1] > h[i]) { s[j] = s[j - 1]; j--; } s[j] = h[i]; }
    const uint8_t *p = u->patch.p;
    unsigned R = (unsigned)(p[TFM_P_ARP_RANGE] % 6) + 1, T = n * R, mode = p[TFM_P_ARP_MODE] % TFM_ARP_MODES;
    uint32_t r = 12345u;
    int walk = 0;
    for (unsigned k = 0; k < steps; k++) {
        unsigned i = 0;
        const uint8_t *src = s;
        r = r * 1664525u + 1013904223u;
        switch (mode) {
        case 0: i = k % T; break;
        case 1: i = T - 1 - k % T; break;
        case 2: { unsigned per = T > 1 ? 2 * T - 2 : 1, j = k % per; i = j < T ? j : per - j; } break;
        case 3: i = (r >> 16) % T; break;
        case 4: walk += (r >> 16) & 1 ? 1 : -1; if (walk < 0) walk = T > 1 ? 1 : 0; if (walk >= (int)T) walk = (int)T - 2 < 0 ? 0 : (int)T - 2; i = (unsigned)walk; break;
        case 5: { unsigned j = k % T; i = j & 1 ? T - 1 - j / 2 : j / 2; } break;
        case 6: { unsigned j = T - 1 - k % T; i = j & 1 ? T - 1 - j / 2 : j / 2; } break;
        case 7: i = k % T; src = h; break;
        case 8: i = (k / 2) % T; break;
        default: i = (k % R) * n; break;   /* ALL: si disegna la nota piu' bassa di ogni ottava */
        }
        out[k] = (uint8_t)(src[i % n] + 12 * (i / n));
    }
    *lo = s[0];
    *hi = (uint8_t)(s[n - 1] + 12 * (R - 1));
    return n;
}
static void tempo_text(tfm_ui *u, int x, int y, unsigned div_param)
{
    char s[20];
    tfm_strcpy(s, tfm_div_names[u->patch.p[div_param] % TFM_DIVS]);
    unsigned l = (unsigned)tfm_strlen(s);
    s[l++] = ' '; s[l++] = '@'; s[l++] = ' ';
    num_text(s + l, 60u + u->patch.p[TFM_P_BPM]);
    x = text(u, x, y, s, 2, TEXT);
    text(u, x + 4, y + 7, "BPM", 1, DIM);
}
static void draw_arp(tfm_ui *u)
{
    const uint8_t *p = u->patch.p;
    header(u, "ARP", u->page[S_ARP] ? "2 GATE & TEMPO" : "1 MODE");
    int on = p[TFM_P_ARP_ON] != 0;
    text(u, 6, 36, on ? "ON" : "OFF", 2, on ? GREEN : DIM);
    if (on && u->arp_busy) text(u, 90, 40, "PLAYING", 1, AMBER);
    if (on && p[TFM_P_ARP_LATCH]) text(u, 140, 40, "LATCH", 1, ACCENT);
    /* anteprima del percorso: 16 passi */
    uint8_t seq[16], lo, hi;
    arp_preview(u, seq, 16, &lo, &hi);
    int bx = 6, by = 56, bw = 228, bh = 84;
    frame(u, bx, by, bw, bh, PANEL);
    int span = hi > lo ? hi - lo : 1;
    float g = 0.05f + (float)p[TFM_P_ARP_GATE] * (0.95f / 127.f);
    int cw = (bw - 8) / 16;
    for (unsigned k = 0; k < 16; k++) {
        int yy = by + bh - 10 - (int)((float)(seq[k] - lo) / (float)span * (float)(bh - 20));
        int w = (int)((float)cw * g);
        if (w < 2) w = 2;
        fill(u, bx + 4 + (int)k * cw, yy, w, 5, on ? knob_color[k % 4] : DIM);
    }
    tempo_text(u, 6, 150, TFM_P_ARP_RATE);
    text(u, 6, 172, u->section == S_ARP ? "ARP BUTTON = ON/OFF" : "", 1, DIM);
    page_dots(u, 2, u->page[S_ARP]);
    footer(u, P_ARP[u->page[S_ARP]]);
}

static void draw_steps(tfm_ui *u, int bx, int by, int bw, int bh, unsigned first, unsigned count, int cursor)
{
    unsigned len = seq_len_ui(u);
    int lo = 127, hi = 0;
    for (unsigned i = 0; i < len; i++) if (u->seq.gate[i]) { if (u->seq.note[i] < lo) lo = u->seq.note[i]; if (u->seq.note[i] > hi) hi = u->seq.note[i]; }
    if (lo > hi) { lo = 48; hi = 72; }
    if (hi - lo < 12) { int m = (lo + hi) / 2; lo = m - 6; hi = m + 6; }
    frame(u, bx, by, bw, bh, PANEL);
    int cw = (bw - 4) / (int)count;
    if (cw < 1) cw = 1;
    for (unsigned k = 0; k < count; k++) {
        unsigned st = first + k;
        if (st >= len) break;
        int x = bx + 2 + (int)k * cw;
        if (cursor && st == u->cursor) fill(u, x, by + 1, cw, bh - 2, PANEL);
        if (u->playing && st == u->play_step) fill(u, x, by + bh - 4, cw, 3, AMBER);
        uint8_t gt = u->seq.gate[st];
        if (gt == TFM_GATE_REST) { fill(u, x, by + bh / 2, cw > 2 ? cw - 1 : 1, 1, DIM); continue; }
        int yy = by + bh - 10 - (int)((float)(u->seq.note[st] - lo) / (float)(hi - lo) * (float)(bh - 20));
        uint16_t c = gt == TFM_GATE_TIE ? ACCENT : (st % 4 == 0 ? GREEN : knob_color[2]);
        int w = gt == TFM_GATE_TIE ? cw : (cw > 2 ? cw - 1 : 1);
        if (gt == TFM_GATE_TIE && k) fill(u, x - 1, yy + 1, 2, 2, ACCENT);
        fill(u, x, yy, w, 4, c);
    }
}
static void draw_seq(tfm_ui *u)
{
    static const char *const sub[3] = { "1 SEQUENCE", "2 TEMPO", "3 STEPS" };
    unsigned pg = u->page[S_SEQ], len = seq_len_ui(u);
    header(u, "SEQ", sub[pg]);
    char s[16];
    text(u, 6, 36, u->playing ? "PLAY" : "STOP", 2, u->playing ? GREEN : DIM);
    if (u->rec) text(u, 66, 36, "REC", 2, RED);
    num_text(s, (u->playing ? u->play_step : u->cursor) + 1u);
    unsigned l = (unsigned)tfm_strlen(s);
    s[l++] = '/';
    num_text(s + l, len);
    text(u, 234 - text_w(s, 2), 36, s, 2, TEXT);
    if (pg == 2) {   /* finestra di 16 passi attorno al cursore */
        unsigned first = (u->cursor / 16) * 16;
        draw_steps(u, 6, 56, 228, 84, first, 16, 1);
        char nn[8];
        if (u->seq.gate[u->cursor] == TFM_GATE_REST) tfm_strcpy(nn, "REST");
        else note_text(nn, u->seq.note[u->cursor]);
        int x = text(u, 6, 148, nn, 3, AMBER);
        text(u, x + 10, 155, L_GATE[u->seq.gate[u->cursor] % 3], 1, ACCENT);
        text(u, 6, 176, u->rec ? "PLAY KEYS: WRITE + NEXT" : "KEYS = STEP NOTE", 1, DIM);
    } else {
        draw_steps(u, 6, 56, 228, 84, 0, len < 16 ? 16 : len, 0);
        if (pg == 0) {
            text(u, 6, 150, u->patch.p[TFM_P_SEQ_NOTE] ? "RELA: KEYS TRANSPOSE" : "ABS: NOTES AS WRITTEN", 1, TEXT);
            text(u, 6, 166, "PLAY = START  REC = WRITE", 1, DIM);
        } else {
            tempo_text(u, 6, 150, TFM_P_SEQ_RATE);
        }
    }
    page_dots(u, 3, pg);
    footer(u, P_SEQ[pg]);
}
static void draw_save(tfm_ui *u)
{
    char s[8] = "U01";
    s[1] = (char)('0' + (u->save_slot + 1) / 10); s[2] = (char)('0' + (u->save_slot + 1) % 10);
    header(u, "SAVE", "SELECT = SLOT");
    int x = text(u, 6, 40, "SLOT", 1, DIM);
    text(u, x + 8, 36, s, 2, ACCENT);
    for (int k = 0; k < 4; k++) {   /* il nome, un carattere per knob */
        char c[2] = { u->save_name[k], 0 };
        int cx = 18 + k * 54;
        fill(u, cx - 4, 120, 40, 3, knob_color[k]);
        text(u, cx, 70, c[0] == ' ' ? "_" : c, 6, TEXT);
    }
    const user_preset *up = &u->user[u->save_slot];
    if (up->used) { x = text(u, 6, 136, "OVERWRITE", 1, AMBER); text(u, x + 8, 136, up->name, 1, TEXT); }
    else text(u, 6, 136, "EMPTY SLOT", 1, GREEN);
    text(u, 6, 156, "SAVE = CONFIRM", 2, TEXT);
    text(u, 6, 178, "OTHER BUTTON = CANCEL", 1, DIM);
    footer(u, P_SAVE);
}
/* GLOBAL pagina 7: la scala sulla tastiera e l'accordo sulla tonica */
static void draw_scale(tfm_ui *u)
{
    const uint8_t *p = u->patch.p;
    unsigned sc = p[TFM_P_SCALE] % TFM_SCALES, root = p[TFM_P_ROOT] % 12, ch = p[TFM_P_CHORD] % TFM_CHORDS;
    uint16_t m = tfm_scale_masks[sc];
    static const uint8_t white[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int8_t bpos[12] = { -1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1 };
    int kx = 9, kw = 31, ky = 40, kh = 90;
    uint8_t cht[12] = { 0 };
    for (unsigned i = 0; i < 5 && tfm_chord_notes[ch][i] >= 0; i++) {   /* l'accordo sulla tonica, corretto dalla scala come nel motore */
        unsigned v = root + (unsigned)tfm_chord_notes[ch][i];
        for (unsigned d = 0; d < 12; d++) if ((m >> ((v - d - root + 24) % 12)) & 1) { v -= d; break; }
        cht[v % 12] = 1;
    }
    for (unsigned w = 0; w < 7; w++) {
        unsigned n = white[w];
        int in = (m >> ((n - root + 12) % 12)) & 1;
        fill(u, kx + (int)w * kw, ky, kw - 2, kh, in ? (n == root ? AMBER : TEXT) : DIM);
        if (cht[n] && ch) fill(u, kx + (int)w * kw + 9, ky + kh - 16, 11, 8, RED);
    }
    for (unsigned n = 0; n < 12; n++) {
        if (bpos[n] < 0) continue;
        int in = (m >> ((n - root + 12) % 12)) & 1;
        int x = kx + (bpos[n] + 1) * kw - 10;
        fill(u, x - 1, ky, 20, 56, BACK);
        fill(u, x, ky, 18, 54, in ? (n == root ? AMBER : knob_color[0]) : th->off);
        if (cht[n] && ch) fill(u, x + 4, ky + 40, 10, 8, RED);
    }
    char s[24];
    tfm_strcpy(s, L_ROOT[root]);
    unsigned l = (unsigned)tfm_strlen(s);
    s[l++] = ' ';
    tfm_strcpy(s + l, tfm_scale_names[sc]);
    int x = text(u, 6, 142, s, 2, TEXT);
    if (ch) { x = text(u, x + 12, 142, tfm_chord_names[ch], 2, RED); }
    text(u, 6, 166, sc ? "KEYS FOLLOW THE SCALE" : "CHROMATIC: NO CORRECTION", 1, DIM);
}

static void draw_info(tfm_ui *u)
{
    static const char *const names[TFM_BTN_COUNT] = { "", "", "", "", "", "", "", "", "", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
    static const char *const what[TFM_BTN_COUNT] = { "", "", "", "", "", "", "", "", "", "SAVE PRESET",
                                                     "ARPEGGIATOR", "SEQUENCER 128", "SEQUENCER", "STEP RECORD" };
    static const char *const phase[TFM_BTN_COUNT] = { "", "", "", "", "", "", "", "", "", "PHASE 5", "PHASE 5", "PHASE 5", "PHASE 5", "PHASE 5" };
    unsigned b = u->info < TFM_BTN_COUNT ? u->info : 0;
    header(u, names[b], 0);
    text(u, 6, 60, what[b], 2, TEXT);
    text(u, 6, 92, "COMING:", 2, DIM);
    text(u, 6, 116, phase[b], 3, AMBER);
    text(u, 6, 164, "HOME TO GO BACK", 1, DIM);
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
    case S_ARP: draw_arp(u); break;
    case S_SEQ: draw_seq(u); break;
    case S_SAVE: draw_save(u); break;
    default: draw_home(u); break;
    }
}

uint32_t tfm_ui_draw(tfm_ui *u)
{
    if (!u->dirty) return 0;
    th = &themes[u->set.theme < 3 ? u->set.theme : 0];
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

/* ------------------------------------------------------------------ */
/* Logo di avvio in stile anni '70 (l'epoca in cui nasce la sintesi FM): */
/* fondo crema, quattro strisce marrone-ruggine-arancio-senape che      */
/* ondeggiano come un'onda FM, scritta grossa con contorno e ombra.     */
/* Disegnato a bande di 16 righe, senza tfm_ui.                         */
/* ------------------------------------------------------------------ */
static uint16_t rgb565(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
#define LOGO_S 5
#define LOGO_X0 18
#define LOGO_Y0 52
/* 0 fuori, 1 ombra, 2 contorno, 3 lettera */
static int logo_text_at(int x, int y)
{
    static const char word[] = "TWISTER";
    int best = 0;
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++) {
            int yy = y + dy - LOGO_Y0, xx = x + dx - LOGO_X0;
            if (yy < 0 || yy >= 7 * LOGO_S || xx < 0) continue;
            int ch = xx / (6 * LOGO_S), col = (xx % (6 * LOGO_S)) / LOGO_S, row = yy / LOGO_S;
            if (ch >= 7 || col >= 5 || !(glyph(word[ch])[col] & (1u << row))) continue;
            if (!dx && !dy) return 3;
            if (dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2) { if (best < 2) best = 2; }
            else if (dx <= -1 && dy <= -1) { if (best < 1) best = 1; }   /* ombra in basso a destra */
        }
    return best;
}
static void logo_small_text(uint16_t *px, unsigned y0, int y, const char *s, uint16_t c)
{
    int w = (int)tfm_strlen(s) * 6, sx = (240 - w) / 2;
    for (int i = 0; s[i]; i++) {
        const uint8_t *g = glyph(s[i]);
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1u << row)) {
                    int yy = y + row - (int)y0;
                    if (yy >= 0 && yy < 16) px[yy * 240 + sx + i * 6 + col] = PXC(c);
                }
    }
}
void tfm_logo_band(uint16_t *px, unsigned y0)
{
    static const uint8_t stripe[4][3] = { { 96, 52, 28 }, { 184, 76, 32 }, { 232, 136, 40 }, { 236, 190, 70 } };
    for (int r = 0; r < 16; r++) {
        int y = (int)y0 + r;
        for (int x = 0; x < 240; x++) {
            uint16_t c = rgb565(246, 234, 206);   /* carta crema */
            /* le strisce: il loro centro segue un'onda FM, sin(a + I sin(b)) */
            float t = (float)x * (1.f / 240.f);
            float wave = sin1(t * 2.f + 0.22f * sin1(t * 1.f + 0.1f));
            int base = 160 + (int)(wave * 16.f);
            int k = (y - base + 24) / 12;
            if (y - base + 24 >= 0 && k >= 0 && k < 4 && ((y - base + 24) % 12) < 10)
                c = rgb565(stripe[k][0], stripe[k][1], stripe[k][2]);
            int tx = logo_text_at(x, y);
            if (tx == 1) c = rgb565(232, 136, 40);
            else if (tx == 2) c = rgb565(70, 36, 20);
            else if (tx == 3) c = rgb565(250, 228, 170);
            px[r * 240 + x] = PXC(c);
        }
    }
    logo_small_text(px, y0, 30, "FREQUENCY MODULATION", rgb565(110, 60, 30));
    logo_small_text(px, y0, 104, "SYNTHESIZER", rgb565(184, 76, 32));
    logo_small_text(px, y0, 214, "8 VOICES  -  OPL3", rgb565(110, 60, 30));
}
