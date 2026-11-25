/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Interfaccia utente "stile TWISTfm" sui comandi reali dell'FM-1:
 * schermo 240x240 RGB565, encoder SELECT/ALGORITHM/K1-K4/PRESETS, 14 tasti,
 * 27 tasti musicali (da FA a SOL). Lo stesso codice gira nel simulatore e,
 * nella fase 3, sul dispositivo. Disegna solo quando qualcosa cambia. */
#ifndef TFM_UI_H
#define TFM_UI_H
#include <stdint.h>
#include "tfm_engine.h"

#define TFM_SCREEN_W 240
#define TFM_SCREEN_H 240

/* Indici come nell'SDK FM-1 (fm1_input.h / Simulator.js). */
enum { TFM_ENC_SELECT, TFM_ENC_ALGORITHM, TFM_ENC_K1, TFM_ENC_K2, TFM_ENC_K3, TFM_ENC_K4, TFM_ENC_PRESETS };
enum { TFM_BTN_OCT_DOWN, TFM_BTN_OCT_UP, TFM_BTN_FX, TFM_BTN_SEL, TFM_BTN_ENV, TFM_BTN_LFO, TFM_BTN_EDIT,
       TFM_BTN_GLO, TFM_BTN_HOME, TFM_BTN_SAVE, TFM_BTN_ARP, TFM_BTN_SEQ, TFM_BTN_PLAY, TFM_BTN_REC, TFM_BTN_COUNT };
#define TFM_KEYS 27

/* Eventi prodotti dall'interfaccia per il motore audio. */
/* PATCH: preset nuovo (parametri e passi: tfm_ui_patch, tfm_ui_seq); STEP: passo a cambiato; SEQ: tutti i passi;
 * PLAY: sequencer avviato (a=1) o fermato; SAVE: salvare lo slot utente a (tfm_ui_user_blob) */
enum { TFM_EV_NONE, TFM_EV_NOTE_ON, TFM_EV_NOTE_OFF, TFM_EV_PARAM, TFM_EV_PATCH, TFM_EV_STEP, TFM_EV_SEQ,
       TFM_EV_PLAY, TFM_EV_SAVE, TFM_EV_SETTINGS };
/* impostazioni globali (MIDI, clock), salvate a parte dai preset */
#define TFM_SETTINGS_MAGIC 0x5Au
typedef struct {
    uint8_t magic;
    uint8_t in_ch;      /* 0 = OMNI, 1..16 */
    uint8_t out_ch;     /* 0..15 = canale 1..16 */
    uint8_t note_out;   /* note dei tasti verso USB */
    uint8_t thru;       /* MIDI in -> USB out */
    uint8_t sync;       /* 0 clock interno, 1 esterno (MIDI clock, START/STOP) */
    uint8_t clk_out;    /* clock e START/STOP verso USB */
    uint8_t theme;      /* 0 DARK, 1 70S */
} tfm_settings;
typedef struct { uint8_t type, a, b; } tfm_event;

typedef struct tfm_ui tfm_ui;
/* trasferisce un rettangolo di pixel allo schermo (sul dispositivo: lcd_blit) */
typedef void (*tfm_blit_fn)(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px);
void tfm_ui_set_blit(tfm_ui *u, tfm_blit_fn fn);
unsigned tfm_ui_size(void);
tfm_ui *tfm_ui_init(void *memory);
void tfm_ui_encoder(tfm_ui *u, unsigned encoder, int delta);
void tfm_ui_button(tfm_ui *u, unsigned button, int down);
void tfm_ui_key(tfm_ui *u, unsigned key, int down);
void tfm_ui_key_vel(tfm_ui *u, unsigned key, int down, unsigned velocity);
/* Nota MIDI esterna: aggiorna solo i LED, il suono va direttamente al motore. */
int tfm_ui_next_event(tfm_ui *u, tfm_event *out);
const tfm_patch *tfm_ui_patch(const tfm_ui *u);
/* Ridisegna se serve; restituisce 0 se nulla e' cambiato, altrimenti il
 * rettangolo modificato come x | y<<8 | (w-1)<<16 | (h-1)<<24. */
uint32_t tfm_ui_draw(tfm_ui *u);
#ifndef TFM_UI_NO_FRAMEBUFFER
const uint16_t *tfm_ui_framebuffer(const tfm_ui *u);
/* logo retro: una banda di 16 righe (240 x 16 pixel) dalla riga y0, anche senza tfm_ui */
void tfm_logo_band(uint16_t *px, unsigned y0);
#endif
/* LED: bit 0..13 tasti funzione, 14..40 tasti musicali. */
uint64_t tfm_ui_leds(const tfm_ui *u);
unsigned tfm_ui_preset_count(void);
/* sequencer e preset utente (fase 5) */
#define TFM_USER_SLOTS 16
const tfm_seq *tfm_ui_seq(const tfm_ui *u);
const tfm_settings *tfm_ui_settings(const tfm_ui *u);
void tfm_ui_settings_load(tfm_ui *u, const tfm_settings *s);
void tfm_ui_set_bpm(tfm_ui *u, unsigned bpm);
void tfm_ui_set_cpu(tfm_ui *u, unsigned pct);   /* carico audio in % (plat_cpu_pct) */   /* tfm_bpm_now() del motore */
int tfm_ui_playing(const tfm_ui *u);
void tfm_ui_set_play_state(tfm_ui *u, unsigned state);   /* tfm_play_state() del motore */
unsigned tfm_ui_user_blob(const tfm_ui *u, unsigned slot, uint8_t *buf, unsigned max);   /* byte da salvare, 0 = vuoto */
int tfm_ui_user_load(tfm_ui *u, unsigned slot, const uint8_t *buf, unsigned len);       /* 0 = ok */
#define TFM_USER_BLOB_MAX 640u
const char *tfm_ui_preset_name(unsigned index);
/* logo retro: una banda di 16 righe (240 x 16 pixel) dalla riga y0, anche senza tfm_ui */
void tfm_logo_band(uint16_t *px, unsigned y0);
#endif
