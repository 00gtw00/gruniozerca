/*
 * config.h — Parser/writer GRUNIO.CFG (format INI)
 * Gruniożerca DOS port
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "input.h"
#include "sound.h"

#define CONFIG_FILE "GRUNIO.CFG"

/* ---------- Kompletna konfiguracja gry ------------------------------------ */
typedef struct {
    /* [input] */
    uint8_t  key_left;      /* scancode — domyślnie SC_LEFT  */
    uint8_t  key_right;     /* scancode — domyślnie SC_RIGHT */
    uint8_t  key_action;    /* scancode — domyślnie SC_Z     */
    uint8_t  key_start;     /* scancode — domyślnie SC_ENTER */
    uint8_t  use_joystick;  /* 0/1 */
    uint8_t  use_mouse;     /* 0/1 */
    uint8_t  mouse_com;     /* 1 = COM1, 2 = COM2 */

    /* [sound] */
    SoundConfig sound;

    /* [video] */
    uint8_t  show_fps;      /* 0/1 — licznik FPS (debug) */
} GameConfig;

/* Globalna konfiguracja */
extern GameConfig g_config;

/* ---------- API ------------------------------------------------------------ */

/* Ładuje konfigurację z GRUNIO.CFG.
   Jeśli plik nie istnieje, wypełnia wartościami domyślnymi. */
void config_load(void);

/* Zapisuje aktualną konfigurację do GRUNIO.CFG. */
void config_save(void);

/* Wypełnia konfigurację wartościami domyślnymi. */
void config_defaults(void);

/* Stosuje załadowaną konfigurację do modułów input/sound. */
void config_apply(void);

#endif /* CONFIG_H */
