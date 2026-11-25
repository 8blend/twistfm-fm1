/* SPDX-License-Identifier: MIT */
/* Motore "stile TWISTfm" per M-VAVE FM-1: 8 voci, layer 1 FM a 4 operatori
 * (5 algoritmi), layer 2 a 2 operatori (serie/parallelo), operatori con le 8
 * forme d'onda OPL3, filtro a 4 poli per voce (15 modi, LIQUID/MS20, HEAT).
 * Tutti i parametri sono "fader" 0..127 come sul pannello originale, salvo
 * dove indicato. Uscita: stereo interleaved signed Q23 a 44.1 kHz. */
#ifndef TFM_ENGINE_H
#define TFM_ENGINE_H
#include <stdint.h>

#define TFM_VOICES 8
#define TFM_OPS 6
#define TFM_SAMPLE_RATE 44100

/* Parametri per operatore: indice = TFM_OP_BASE(op) + campo, op 0..5 (OP1..OP6). */
enum {
    TFM_OPP_VOL,   /* volume (total level) */
    TFM_OPP_WAVE,  /* 8 forme d'onda OPL3 */
    TFM_OPP_MULT,  /* moltiplicatore 0.5..15 */
    TFM_OPP_A, TFM_OPP_D, TFM_OPP_S, TFM_OPP_R,
    TFM_OPP_KSL,   /* 0..3 = 0, -1.5, -3, -6 dB/ottava */
    TFM_OPP_KSR,   /* 0/1 */
    TFM_OPP_KVS,   /* sensibilita' velocity 0..10 */
    TFM_OPP_COUNT
};
#define TFM_OP_BASE(op) ((op) * TFM_OPP_COUNT)

enum {
    TFM_P_ALGO = TFM_OP_BASE(TFM_OPS), /* layer 1: 0..9 (0..4 originali, 5..9 extra) */
    TFM_P_FB1,        /* feedback OP1 */
    TFM_P_ALGO2,      /* layer 2: 0 serie, 1 parallelo */
    TFM_P_FB2,        /* feedback OP5 */
    TFM_P_L2_TUNE,    /* layer 2: semitoni, 64 = 0, range +-24 */
    TFM_P_FLT_TYPE,   /* 0..14: LP4 LP3 LP2 LP1 BP4 BP2 NOTCH PHASE HP1 HP2 HP3 H2+L H3+L NO+L PH+L */
    TFM_P_CUTOFF,
    TFM_P_RES,
    TFM_P_FLT_ENV,    /* profondita' inviluppo filtro */
    TFM_P_FLT_A, TFM_P_FLT_D, TFM_P_FLT_S, TFM_P_FLT_R,
    TFM_P_HEAT,       /* drive */
    TFM_P_FLAVOUR,    /* 0 LIQUID, 1 MS20 */
    TFM_P_WOBBLE,
    TFM_P_KEYTRACK,   /* 0..10 */
    TFM_P_FAT,        /* detune tra le voci */
    TFM_P_VOICE_MODE, /* 0 POL8, 1 UNIS, 2 4X2, 3 2X4, 4 MONO, 5 WIDE8, 6 ROUND ROBIN */
    TFM_P_VOLUME,
    TFM_P_VOICES,     /* polifonia massima 1..8 (POL8, WIDE8, ROUND ROBIN) */
    TFM_P_PRIO,       /* priorita' mono: 0 LAST, 1 HIGH, 2 LOW */
    /* effetti (MIX = 0 spegne l'effetto) */
    TFM_P_DIST_TYPE, TFM_P_DIST_DRIVE, TFM_P_DIST_TEMP, TFM_P_DIST_MIX,
    TFM_P_PH_RATE, TFM_P_PH_DEPTH, TFM_P_PH_FB, TFM_P_PH_MIX,
    TFM_P_DLY_TIME, TFM_P_DLY_FB, TFM_P_DLY_TONE, TFM_P_DLY_MIX, TFM_P_DLY_WOW, TFM_P_DLY_SAT, TFM_P_DLY_SPREAD,
    TFM_P_REV_TYPE, TFM_P_REV_DECAY, TFM_P_REV_TONE, TFM_P_REV_MIX, TFM_P_REV_PREDLY, TFM_P_REV_MOD, TFM_P_REV_SHIM,
    /* catena: effetto in ogni slot, 0 vuoto, 1 DIST, 2 PHASER, 3 TAPE, 4 REVERB */
    TFM_P_FX_SLOT1, TFM_P_FX_SLOT2, TFM_P_FX_SLOT3, TFM_P_FX_SLOT4,
    /* --- modulazioni (fase 4) --- */
    /* 3 LFO: velocita', forma (TRI SAW RAMP RECT NOIZ SINE), modo (BIPO UNI+ UNI- REC+ REC-), retrigger */
    TFM_P_LFO1_RATE, TFM_P_LFO1_SHAPE, TFM_P_LFO1_MODE, TFM_P_LFO1_RETRIG,
    TFM_P_LFO2_RATE, TFM_P_LFO2_SHAPE, TFM_P_LFO2_MODE, TFM_P_LFO2_RETRIG,
    TFM_P_LFO3_RATE, TFM_P_LFO3_SHAPE, TFM_P_LFO3_MODE, TFM_P_LFO3_RETRIG,
    /* chain: 4 collegamenti per LFO, ognuno destinazione (TFM_MOD_*) e quantita' (0..40, 20 = zero: -20..+20) */
    TFM_P_LFO1_LINKS, TFM_P_LFO2_LINKS = TFM_P_LFO1_LINKS + 8, TFM_P_LFO3_LINKS = TFM_P_LFO2_LINKS + 8,
    /* inviluppo di modulazione per voce: ADSR e 4 collegamenti */
    TFM_P_MENV_A = TFM_P_LFO3_LINKS + 8, TFM_P_MENV_D, TFM_P_MENV_S, TFM_P_MENV_R,
    TFM_P_MENV_LINKS,
    /* vibrato: velocita', profondita', forma (SINE RECT SAW RAMP NOIZ), modo (BIPO UP DOWN) */
    TFM_P_VIB_RATE = TFM_P_MENV_LINKS + 8, TFM_P_VIB_DEPTH, TFM_P_VIB_WAVE, TFM_P_VIB_MODE,
    /* tremolo stereo: velocita', profondita', sfasamento L/R */
    TFM_P_TREM_RATE, TFM_P_TREM_DEPTH, TFM_P_TREM_PHASE,
    /* glide (0 spento), modo (ALL LEGATO), rise e fall del pitch */
    TFM_P_GLIDE, TFM_P_GLIDE_MODE, TFM_P_RISE, TFM_P_FALL,
    /* --- fase 5 --- */
    /* tempo: BPM = 60 + valore (60..187), swing 0..127 */
    TFM_P_BPM, TFM_P_SWING,
    /* arpeggiatore: acceso, modo (TFM_ARP_*), ottave 0..5 (1..6), divisione (TFM_DIVS), gate 0..127, latch */
    TFM_P_ARP_ON, TFM_P_ARP_MODE, TFM_P_ARP_RANGE, TFM_P_ARP_RATE, TFM_P_ARP_GATE, TFM_P_ARP_LATCH,
    /* sequencer: note (0 ABS, 1 RELA), lunghezza-1 (0..127), direzione (FWD REV PING RAND), divisione, gate */
    TFM_P_SEQ_NOTE, TFM_P_SEQ_LEN, TFM_P_SEQ_DIR, TFM_P_SEQ_RATE, TFM_P_SEQ_GATE,
    /* scala (TFM_SCALES), tonica 0..11 (DO..SI), accordo (TFM_CHORDS) */
    TFM_P_SCALE, TFM_P_ROOT, TFM_P_CHORD,
    TFM_P_COUNT
};

#define TFM_FLT_MODES 15
#define TFM_ARP_MODES 10   /* UP DOWN UPDN RAND WALK CONV DIVG PLAY STUT ALL */
#define TFM_DIVS 12        /* 4BAR 2BAR 1BAR 1/2 1/3 1/4 1/6 1/8 1/12 1/16 1/24 1/32 */
#define TFM_SCALES 22
#define TFM_CHORDS 12
#define TFM_SEQ_STEPS 128
enum { TFM_GATE_REST, TFM_GATE_TRIG, TFM_GATE_TIE };
/* passi del sequencer: nota e tipo (TFM_GATE_*) */
typedef struct { uint8_t note[TFM_SEQ_STEPS]; uint8_t gate[TFM_SEQ_STEPS]; } tfm_seq;
extern const char *const tfm_div_names[TFM_DIVS];
extern const char *const tfm_arp_names[TFM_ARP_MODES];
extern const char *const tfm_scale_names[TFM_SCALES];
extern const char *const tfm_chord_names[TFM_CHORDS];
extern const uint16_t tfm_scale_masks[TFM_SCALES];   /* bit i = semitono i dalla tonica */
extern const int8_t tfm_chord_notes[TFM_CHORDS][5];  /* intervalli, -1 = fine */
#define TFM_VOICE_MODES 7
#define TFM_LFOS 3
#define TFM_LFO_BASE(k) (TFM_P_LFO1_RATE + 4 * (k))
#define TFM_LFO_LINK(k, i) (TFM_P_LFO1_LINKS + 8 * (k) + 2 * (i))   /* destinazione; +1 = quantita' */
#define TFM_MENV_LINK(i) (TFM_P_MENV_LINKS + 2 * (i))
#define TFM_LINKS 4
#define TFM_AMT_ZERO 20
#define TFM_LFO_SHAPES 6
#define TFM_LFO_MODES 5
/* destinazioni delle chain */
enum { TFM_MOD_NONE, TFM_MOD_CUTOFF, TFM_MOD_RES, TFM_MOD_HEAT, TFM_MOD_FENV, TFM_MOD_WOBBLE,
       TFM_MOD_OPVOL, TFM_MOD_OPWAVE = TFM_MOD_OPVOL + 6, TFM_MOD_OPMULT = TFM_MOD_OPWAVE + 6,
       TFM_MOD_FB1 = TFM_MOD_OPMULT + 6, TFM_MOD_FB5, TFM_MOD_PITCH, TFM_MOD_L2TUNE, TFM_MOD_FAT, TFM_MOD_VOLUME,
       TFM_MOD_DRIVE, TFM_MOD_PHDEPTH, TFM_MOD_DLYTIME, TFM_MOD_REVMIX, TFM_MOD_TARGETS };
extern const char *const tfm_mod_target_names[TFM_MOD_TARGETS];
/* algoritmi layer 1: 0..4 come la TWISTfm, 5..9 extra */
#define TFM_ALGOS 10
#define TFM_ALGOS_ORIGINAL 5

typedef struct { uint8_t p[TFM_P_COUNT]; } tfm_patch;

struct tfm_engine;
typedef struct tfm_engine tfm_engine;

/* Dimensione della memoria che il chiamante deve fornire (allineata a 8). */
unsigned tfm_engine_size(void);
tfm_engine *tfm_engine_init(void *memory);
void tfm_load_patch(tfm_engine *e, const tfm_patch *patch);
void tfm_set_param(tfm_engine *e, unsigned id, uint8_t value);
uint8_t tfm_get_param(const tfm_engine *e, unsigned id);
void tfm_note_on(tfm_engine *e, uint8_t note, uint8_t velocity);
void tfm_note_off(tfm_engine *e, uint8_t note);
void tfm_all_notes_off(tfm_engine *e);
void tfm_pitch_bend(tfm_engine *e, int semitones_q16);
/* sequencer: un passo, tutti i passi, avvio/arresto; stato = passo | in corsa << 8 | arp attivo << 9 */
void tfm_set_step(tfm_engine *e, unsigned step, uint8_t note, uint8_t gate);
void tfm_load_seq(tfm_engine *e, const tfm_seq *s);
void tfm_transport(tfm_engine *e, int run);
unsigned tfm_play_state(const tfm_engine *e);
void tfm_seq_default(tfm_seq *s);
/* frames multiplo qualsiasi; output: frames*2 parole Q23 interleaved L/R. */
void tfm_render(tfm_engine *e, int32_t *output, unsigned frames);
/* Patch iniziale (equivalente di init.tfm). */
void tfm_patch_init(tfm_patch *patch);
const char *tfm_filter_mode_name(unsigned mode);
const char *tfm_wave_name(unsigned wave);

#endif
