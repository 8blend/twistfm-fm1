/* SPDX-License-Identifier: GPL-3.0-only */
/* Firmware "stile TwistFM" per M-VAVE FM-1: collega il motore (src/tfm_engine.c, tfm_fx.c) e l'interfaccia
 * (src/tfm_ui.c) alla piattaforma fm1-base. Fa parte della unity build (device/unity.c), dopo la piattaforma:
 * lcd.c, gfx.c e touch sono visibili qui.
 *
 * Contesti:
 *  - interrupt audio (app_render): il motore suona; legge la coda di eventi e il MIDI in ingresso;
 *  - ciclo principale (app_frame): tasti, encoder, schermo; scrive solo nella coda di eventi.
 * La coda e' a un produttore e un consumatore, senza lock. */
#include "src/tfm_engine.h"
#include "src/tfm_ui.h"

/* ------------------------------------------------------------------ memoria --- */
#define ENG_BYTES (192u * 1024u)        /* tfm_engine_size(): ~187 KB, effetti compresi */
#define UI_BYTES (26u * 1024u)          /* tfm_ui_size() senza framebuffer: ~23 KB (16 preset utente compresi) */
static uint8_t eng_mem[ENG_BYTES] __attribute__((aligned(8))) PLAT_POOL;
static uint8_t ui_mem[UI_BYTES] __attribute__((aligned(8))) PLAT_POOL;
static tfm_engine *E;
static tfm_ui *U;
static uint8_t mem_ok;

/* ------------------------------------------------- coda eventi UI -> audio --- */
enum { Q_NOTE_ON = 1, Q_NOTE_OFF, Q_PARAM, Q_ALL_OFF, Q_STEP, Q_SEQ, Q_PLAY };
#define QN 512u
static volatile uint32_t q_buf[QN];
static volatile uint32_t q_head, q_tail;       /* head: scrive la UI; tail: legge l'audio */
static volatile uint8_t panic_req;

static void q_push(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t h = q_head, next = (h + 1u) & (QN - 1u);
    if (next == q_tail)
        return;                                 /* piena: si perde l'evento, mai bloccare */
    q_buf[h] = type | a << 8 | b << 16;
    q_head = next;
}

/* ------------------------------------------------------------------- audio --- */
static float gain_now;

static uint8_t run_out;                        /* stato del sequencer gia' comunicato al clock out */

static void midi_in(void)                      /* nell'interrupt audio: il MIDI arrivato da USB e TRS */
{
    uint32_t pkt;
    int port;
    const tfm_settings *set = tfm_ui_settings(U);
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24 & 0x7Fu;
        if (set->thru) plat_midi_send(pkt, TO_USB & ~plat_midi_in_from());   /* MIDI thru verso il computer */
        if (st >= 0xF8u) {                      /* real-time: clock e trasporto (solo con sync esterno) */
            if (!set->sync) continue;
            if (st == 0xF8u) tfm_midi_clock(E);
            else if (st == 0xFAu) { tfm_transport(E, 0); tfm_transport(E, 1); }
            else if (st == 0xFBu) tfm_transport(E, 1);
            else if (st == 0xFCu) tfm_transport(E, 0);
            continue;
        }
        if (st < 0xF0u && set->in_ch && (st & 0x0Fu) != (uint32_t)(set->in_ch - 1u))
            continue;                           /* canale diverso da quello scelto */
        switch (st & 0xF0u) {
        case 0x90u:
            if (d2) tfm_note_on(E, (uint8_t)d1, (uint8_t)d2);
            else tfm_note_off(E, (uint8_t)d1);  /* velocity 0 = note off */
            break;
        case 0x80u: tfm_note_off(E, (uint8_t)d1); break;
        case 0xB0u: if (d1 == 120u || d1 == 123u) tfm_all_notes_off(E); break;
        case 0xE0u: tfm_pitch_bend(E, (int)(((int)(d2 << 7 | d1) - 8192) * 8)); break;   /* tutta la corsa = +-65536 */
        default: break;
        }
    }
}

static void clock_out(void)                    /* nell'interrupt audio, dopo il render: clock e START/STOP */
{
    const tfm_settings *set = tfm_ui_settings(U);
    uint32_t n = tfm_clock_out(E), run = (tfm_play_state(E) >> 8) & 1u;
    if (!set->clk_out || set->sync)
        return;
    if (run != run_out) {
        plat_midi_send(0x0Fu | (run ? 0xFAu : 0xFCu) << 8, TO_USB);
        run_out = (uint8_t)run;
    }
    while (n--)
        plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    if (!mem_ok) {
        for (i = 0; i < 2u * n; i++) lr[i] = 0;
        return;
    }
    if (panic_req) {
        panic_req = 0;
        tfm_all_notes_off(E);
    }
    while (q_tail != q_head) {                  /* gli eventi della UI, nell'ordine */
        uint32_t t = q_tail, ev = q_buf[t];
        uint32_t type = ev & 0xFFu, a = (ev >> 8) & 0xFFu, b = (ev >> 16) & 0xFFu;
        if (type == Q_NOTE_ON) tfm_note_on(E, (uint8_t)a, (uint8_t)b);
        else if (type == Q_NOTE_OFF) tfm_note_off(E, (uint8_t)a);
        else if (type == Q_PARAM) tfm_set_param(E, a, (uint8_t)b);
        else if (type == Q_ALL_OFF) tfm_all_notes_off(E);
        else if (type == Q_STEP) {              /* un passo cambiato: il valore e' nella UI */
            const tfm_seq *s = tfm_ui_seq(U);
            tfm_set_step(E, a, s->note[a & 127u], s->gate[a & 127u]);
        }
        else if (type == Q_SEQ) tfm_load_seq(E, tfm_ui_seq(U));
        else if (type == Q_PLAY) tfm_transport(E, (int)a);
        q_tail = (t + 1u) & (QN - 1u);
    }
    tfm_set_sync(E, tfm_ui_settings(U)->sync);
    midi_in();
    tfm_render(E, lr, n);
    clock_out();
    /* MASTER: guadagno che scivola, niente scatti a fine blocco (in float: niente prodotti a 64 bit) */
    {
        float target = (float)master_q12 * (1.0f / 4096.0f);
        for (i = 0; i < n; i++) {
            gain_now += (target - gain_now) * 0.004f;
            lr[2 * i] = (int32_t)((float)lr[2 * i] * gain_now);
            lr[2 * i + 1] = (int32_t)((float)lr[2 * i + 1] * gain_now);
        }
    }
}

void app_panic(void) { panic_req = 1; }

/* -------------------------------------------------------------------- avvio --- */
void app_init(void)
{
    mem_ok = tfm_engine_size() <= sizeof eng_mem && tfm_ui_size() <= sizeof ui_mem;
    if (!mem_ok)
        return;
    E = tfm_engine_init(eng_mem);
    U = tfm_ui_init(ui_mem);
    {   /* i preset utente salvati (U01..U16) */
        static uint8_t buf[TFM_USER_BLOB_MAX];
        uint32_t s;
        for (s = 0; s < TFM_USER_SLOTS; s++) {
            int n = plat_store_load(OBJ_USER0 + s, buf, sizeof buf);
            if (n > 0) tfm_ui_user_load(U, s, buf, (unsigned)n);
        }
    }
    {   /* impostazioni MIDI e clock */
        tfm_settings s;
        if (plat_store_load(OBJ_SETTINGS, &s, sizeof s) == (int)sizeof s) tfm_ui_settings_load(U, &s);
    }
    tfm_load_patch(E, tfm_ui_patch(U));        /* il preset di partenza scelto dall'interfaccia */
    tfm_load_seq(E, tfm_ui_seq(U));
}

/* ------------------------------------------------------------------- schermo --- */
static void blit_band(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px)
{
    lcd_blit(x, y, w, h, px);                   /* DMA: tfm_ui alterna due bande, mai quella in volo */
}

static char say_a[24], say_b[24];
static uint32_t say_until;

void app_say(const char *a, const char *b)     /* messaggi della piattaforma (ripristini, risorse...) */
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++) say_a[i] = a[i];
    say_a[i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) say_b[i] = b[i];
    say_b[i] = 0;
    say_until = plat_ms() + 1500u;
}

static void say_draw(void)
{
    cv_begin(240, 32, C_BLACK);
    {
        int32_t x = cv_text(8, 6, &FONT_B, say_a, C_WHITE);
        cv_text(x + 8, 6, &FONT_B, say_b, C_HI);
    }
    cv_blit(0, 0);
}

void app_ui_init(void)
{
    touch_on(1);                                /* velocity dai tasti */
    lcd_fill(0, 0, 240, 240, 0x0000);
    if (!mem_ok) {
        app_say("OUT OF", "MEMORY");
        say_draw();
        return;
    }
    tfm_ui_set_blit(U, blit_band);
}

/* -------------------------------------------------------------------- input --- */
/* da plat.h (B_FX .. B_OCTUP, EN_SELECT .. EN_K4) agli indici dell'interfaccia (TFM_BTN_*, TFM_ENC_*) */
static const uint8_t BTN_MAP[NB] = { TFM_BTN_FX, TFM_BTN_SEL, TFM_BTN_ENV, TFM_BTN_LFO, TFM_BTN_EDIT, TFM_BTN_GLO,
                                     TFM_BTN_HOME, TFM_BTN_SAVE, TFM_BTN_ARP, TFM_BTN_SEQ, TFM_BTN_PLAY, TFM_BTN_REC,
                                     TFM_BTN_OCT_DOWN, TFM_BTN_OCT_UP };
static const uint8_t ENC_MAP[NE] = { TFM_ENC_SELECT, TFM_ENC_ALGORITHM, TFM_ENC_PRESETS, TFM_ENC_K1, TFM_ENC_K2,
                                     TFM_ENC_K3, TFM_ENC_K4 };
static uint32_t prev_keys, prev_btn;

static uint8_t blob[TFM_USER_BLOB_MAX];        /* un preset utente come sta in flash */
static uint32_t set_due;                       /* quando salvare le impostazioni (0 = niente da salvare) */

static void forward_events(void)               /* dall'interfaccia al motore (e le note anche al computer) */
{
    tfm_event ev;
    while (tfm_ui_next_event(U, &ev)) {
        if (ev.type == TFM_EV_NOTE_ON) {
            q_push(Q_NOTE_ON, ev.a, ev.b);
            if (tfm_ui_settings(U)->note_out)
                plat_midi_send(0x09u | (0x90u | tfm_ui_settings(U)->out_ch) << 8 | (uint32_t)ev.a << 16 | (uint32_t)ev.b << 24, TO_USB);
        } else if (ev.type == TFM_EV_NOTE_OFF) {
            q_push(Q_NOTE_OFF, ev.a, 0);
            if (tfm_ui_settings(U)->note_out)
                plat_midi_send(0x08u | (0x80u | tfm_ui_settings(U)->out_ch) << 8 | (uint32_t)ev.a << 16, TO_USB);
        } else if (ev.type == TFM_EV_PARAM) {
            q_push(Q_PARAM, ev.a, ev.b);
        } else if (ev.type == TFM_EV_PATCH) {   /* preset nuovo: tutti i parametri */
            const tfm_patch *p = tfm_ui_patch(U);
            uint32_t i;
            q_push(Q_ALL_OFF, 0, 0);
            for (i = 0; i < TFM_P_COUNT; i++) q_push(Q_PARAM, i, p->p[i]);
            q_push(Q_SEQ, 0, 0);
        } else if (ev.type == TFM_EV_STEP) {
            q_push(Q_STEP, ev.a, 0);
        } else if (ev.type == TFM_EV_SEQ) {
            q_push(Q_SEQ, 0, 0);
        } else if (ev.type == TFM_EV_PLAY) {
            q_push(Q_PLAY, ev.a, 0);
        } else if (ev.type == TFM_EV_SAVE) {    /* preset utente in flash (ciclo principale: qui si puo') */
            uint32_t n = tfm_ui_user_blob(U, ev.a, blob, sizeof blob);
            if (!n || plat_store_save(OBJ_USER0 + ev.a, blob, n) != 0) {
                app_say("SAVE", "FAILED");
            }
        } else if (ev.type == TFM_EV_SETTINGS) {   /* MIDI/clock cambiati: si salvano fra 2 secondi */
            set_due = plat_ms() + 2000u;
            if (!set_due) set_due = 1;
        }
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    if (!mem_ok)
        return;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(prev_keys & bit))
            tfm_ui_key_vel(U, k, 1, touch_vel(k));
        else if (!(keys & bit) && (prev_keys & bit))
            tfm_ui_key_vel(U, k, 0, 0);
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) != (prev_btn & bit))
            tfm_ui_button(U, BTN_MAP[k], (btn & bit) != 0);
    }
    for (k = 0; k < NE; k++) {
        int32_t d = plat_enc((int)k);
        if (d)
            tfm_ui_encoder(U, ENC_MAP[k], d);
    }
    prev_keys = keys;
    prev_btn = btn;
    forward_events();
    tfm_ui_set_play_state(U, tfm_play_state(E));   /* passo del sequencer per lo schermo */
    tfm_ui_set_bpm(U, tfm_bpm_now(E));
    if (set_due && (int32_t)(plat_ms() - set_due) >= 0) {   /* impostazioni MIDI in flash, a bocce ferme */
        set_due = 0;
        plat_store_save(OBJ_SETTINGS, tfm_ui_settings(U), sizeof(tfm_settings));
    }
    {   /* luci: dai bit dell'interfaccia (tasti 0..13, note 14..40) ai bit di plat.h */
        uint64_t l = tfm_ui_leds(U);
        uint32_t lb = 0;
        for (k = 0; k < NB; k++)
            if ((l >> BTN_MAP[k]) & 1u) lb |= 1u << k;
        plat_glow(1);
        plat_leds(lb, (uint32_t)(l >> 14) & ((1u << NKEYS) - 1u));
    }
}

void app_input_only(void) { input(); }

void app_frame(void)
{
    input();
    if (!mem_ok)
        return;
    if (say_until) {
        if (plat_ms() < say_until) { say_draw(); return; }
        say_until = 0;
        tfm_ui_set_blit(U, blit_band);          /* lo schermo intero di nuovo */
    }
    tfm_ui_draw(U);                             /* disegna solo se qualcosa e' cambiato */
}
