/* SPDX-License-Identifier: GPL-3.0-only */
/* Firmware TWISTER ("stile TwistFM") per M-VAVE FM-1, sulla piattaforma fm1-base (MPL-2.0).
 * Questo header e' la meta' dell'app del contratto con la piattaforma (device/unity.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TWISTER"
#define APP_SPLASH_RGB RGB(253, 160, 0)
#define APP_BLOCK 256u                   /* frame per blocco audio: 5.8 ms a 44.1 kHz */
#define APP_TOUCH                        /* velocity dai tasti (lib/touch.h) */
#define APP_CANVAS_PX (240u * 40u)       /* lo schermo lo disegniamo noi a bande: alla piattaforma basta poco */

/* oggetti salvati in flash: le impostazioni dell'app (per i preset, piu' avanti) e la calibrazione del pannello */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* il contratto: la piattaforma chiama queste */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
