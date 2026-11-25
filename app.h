/* SPDX-License-Identifier: GPL-3.0-only */
/* Firmware TWISTER ("stile TwistFM") per M-VAVE FM-1, sulla piattaforma fm1-base (MPL-2.0).
 * Questo header e' la meta' dell'app del contratto con la piattaforma (device/unity.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TWISTER"
#define APP_SPLASH_RGB RGB(253, 160, 0)
#define APP_SPLASH                       /* schermata di avvio nostra: il logo retro (app_splash) */
#define APP_BLOCK 256u                   /* frame per blocco audio: 5.8 ms a 44.1 kHz */
#define APP_TOUCH                        /* velocity dai tasti (lib/touch.h) */
#define APP_CANVAS_PX (240u * 40u)       /* lo schermo lo disegniamo noi a bande: alla piattaforma basta poco */

/* oggetti salvati in flash: le impostazioni dell'app, la calibrazione del pannello e i 16 preset utente
 * (U01..U16), ognuno con la sua coppia di settori da 4 KiB nell'area dati, sotto quella del pannello */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_USER0, OBJ_NOBJ = OBJ_USER0 + 16 };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, \
    0xBC000u, 0xBE000u, 0xC0000u, 0xC2000u, 0xC4000u, 0xC6000u, 0xC8000u, 0xCA000u, \
    0xCC000u, 0xCE000u, 0xD0000u, 0xD2000u, 0xD4000u, 0xD6000u, 0xD8000u, 0xDA000u}

/* il contratto: la piattaforma chiama queste */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);
