/*
 * config.c — Parser/writer GRUNIO.CFG (format INI prosty)
 * Gruniożerca DOS port, 2024
 */
#include "config.h"
#include "input.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

GameConfig g_config;

void config_defaults(void) {
    memset(&g_config, 0, sizeof(g_config));
    g_config.key_left    = SC_LEFT;
    g_config.key_right   = SC_RIGHT;
    g_config.key_action  = SC_SPACE;
    g_config.key_start   = SC_ENTER;
    g_config.use_joystick = 0;
    g_config.use_mouse   = 0;
    g_config.mouse_com   = 1;
    g_config.sound.type  = SND_OPL2;
    g_config.sound.port  = 0x388;
    g_config.sound.irq   = 0;
    g_config.sound.dma   = 0;
    g_config.sound.vol_music = 80;
    g_config.sound.vol_sfx   = 70;
    g_config.show_fps    = 0;
}

void config_load(void) {
    config_defaults();
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return;

    char line[128], section[32] = "";
    while (fgets(line, sizeof(line), f)) {
        /* Usuń whitespace i komentarze */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || *p == '\n') continue;

        /* Sekcja [section] */
        if (*p == '[') {
            sscanf(p, "[%31[^]]", section);
            continue;
        }

        char key[64], val[64];
        if (sscanf(p, "%63[^=]=%63s", key, val) != 2) continue;

        /* Usuń trailing whitespace z klucza */
        char *kend = key + strlen(key) - 1;
        while (kend > key && (*kend == ' ' || *kend == '\t')) *kend-- = '\0';

        if (strcmp(section, "input") == 0) {
            if      (strcmp(key, "key_left")     == 0) g_config.key_left    = (uint8_t)atoi(val);
            else if (strcmp(key, "key_right")    == 0) g_config.key_right   = (uint8_t)atoi(val);
            else if (strcmp(key, "key_action")   == 0) g_config.key_action  = (uint8_t)atoi(val);
            else if (strcmp(key, "key_start")    == 0) g_config.key_start   = (uint8_t)atoi(val);
            else if (strcmp(key, "joystick")     == 0) g_config.use_joystick = (uint8_t)atoi(val);
            else if (strcmp(key, "mouse_com")    == 0) g_config.mouse_com   = (uint8_t)atoi(val);
            else if (strcmp(key, "use_mouse")    == 0) g_config.use_mouse   = (uint8_t)atoi(val);
        }
        else if (strcmp(section, "sound") == 0) {
            if      (strcmp(key, "card")      == 0) {
                if      (strcmp(val, "SB")      == 0) g_config.sound.type = SND_SB;
                else if (strcmp(val, "SB16")    == 0) g_config.sound.type = SND_SB16;
                else if (strcmp(val, "OPL2")    == 0) g_config.sound.type = SND_OPL2;
                else if (strcmp(val, "SPEAKER") == 0) g_config.sound.type = SND_SPEAKER;
                else                                  g_config.sound.type = SND_NONE;
            }
            else if (strcmp(key, "port")      == 0) g_config.sound.port = (uint16_t)strtol(val, NULL, 16);
            else if (strcmp(key, "irq")       == 0) g_config.sound.irq  = (uint8_t)atoi(val);
            else if (strcmp(key, "dma")       == 0) g_config.sound.dma  = (uint8_t)atoi(val);
            else if (strcmp(key, "vol_music") == 0) g_config.sound.vol_music = (uint8_t)atoi(val);
            else if (strcmp(key, "vol_sfx")   == 0) g_config.sound.vol_sfx   = (uint8_t)atoi(val);
        }
        else if (strcmp(section, "video") == 0) {
            if (strcmp(key, "show_fps") == 0) g_config.show_fps = (uint8_t)atoi(val);
        }
    }
    fclose(f);
}

void config_save(void) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) return;

    static const char *card_names[] = {
        "NONE", "SPEAKER", "OPL2", "OPL3", "SB", "SB_PRO", "SB16"
    };

    fprintf(f, "; Gruniozerca DOS - konfiguracja\n");
    fprintf(f, "; Wygenerowane automatycznie przez SETUP.EXE\n\n");
    fprintf(f, "[input]\n");
    fprintf(f, "key_left=%d\n",   g_config.key_left);
    fprintf(f, "key_right=%d\n",  g_config.key_right);
    fprintf(f, "key_action=%d\n", g_config.key_action);
    fprintf(f, "key_start=%d\n",  g_config.key_start);
    fprintf(f, "joystick=%d\n",   g_config.use_joystick);
    fprintf(f, "use_mouse=%d\n",  g_config.use_mouse);
    fprintf(f, "mouse_com=%d\n",  g_config.mouse_com);
    fprintf(f, "\n[sound]\n");
    int card_idx = (int)g_config.sound.type;
    if (card_idx < 0 || card_idx > 6) card_idx = 0;
    fprintf(f, "card=%s\n",       card_names[card_idx]);
    fprintf(f, "port=0x%X\n",     g_config.sound.port);
    fprintf(f, "irq=%d\n",        g_config.sound.irq);
    fprintf(f, "dma=%d\n",        g_config.sound.dma);
    fprintf(f, "vol_music=%d\n",  g_config.sound.vol_music);
    fprintf(f, "vol_sfx=%d\n",    g_config.sound.vol_sfx);
    fprintf(f, "\n[video]\n");
    fprintf(f, "show_fps=%d\n",   g_config.show_fps);
    fclose(f);
}

void config_apply(void) {
    /* Zastosuj konfigurację klawiatury */
    action_key[ACT_LEFT]   = g_config.key_left;
    action_key[ACT_RIGHT]  = g_config.key_right;
    action_key[ACT_ACTION] = g_config.key_action;
    action_key[ACT_START]  = g_config.key_start;

    if (!g_config.use_joystick) input_disable_device(INPUT_DEV_JOYSTICK);
    if (!g_config.use_mouse)    input_disable_device(INPUT_DEV_MOUSE);
}
