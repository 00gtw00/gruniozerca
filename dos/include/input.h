/*
 * input.h — Interfejs wejścia: klawiatura, joystick (Game Port $201), mysz serial
 * Gruniożerca DOS port
 *
 * Port z nes/Sys/KEYPAD.asm:
 *  - Edge-detection: keys_pressed[] = nowe naciśnięcia w tej klatce
 *  - keys_held[] = stan ciągły (trzymany)
 *
 * Joystick: Game Port $201 (standard PC), 2 osie analogowe + 2 przyciski.
 * Mysz: protokół Microsoft Mouse Serial (3-bajtowy, 1200 baud), COM1 lub COM2.
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* ---------- Kody akcji gry (niezależne od fizycznego klawisza) ------------- */
typedef enum {
    ACT_LEFT   = 0,
    ACT_RIGHT  = 1,
    ACT_ACTION = 2,   /* A/Z — zmiana koloru, potwierdzenie */
    ACT_START  = 3,   /* Enter/Esc — pauza, start */
    ACT_SELECT = 4,   /* Tab — select */
    ACT_COUNT
} GameAction;

/* ---------- Scancodes PC (Set 1) — wartości domyślne ---------------------- */
#define SC_LEFT    0x4B
#define SC_RIGHT   0x4D
#define SC_UP      0x48
#define SC_DOWN    0x50
#define SC_ENTER   0x1C
#define SC_ESC     0x01
#define SC_SPACE   0x39
#define SC_Z       0x2C
#define SC_X       0x2D
#define SC_TAB     0x0F
#define SC_F1      0x3B
#define SC_F2      0x3C
#define SC_F10     0x44

/* Maksymalna liczba scancodes */
#define KEY_COUNT  128

/* ---------- Typy urządzeń wejściowych ------------------------------------- */
typedef enum {
    INPUT_DEV_KEYBOARD = 0,
    INPUT_DEV_JOYSTICK = 1,
    INPUT_DEV_MOUSE    = 2
} InputDevice;

/* ---------- Stan klawiatury ----------------------------------------------- */
extern uint8_t keys_held[KEY_COUNT];    /* 1 = trzymany */
extern uint8_t keys_pressed[KEY_COUNT]; /* 1 = naciśnięty w tej klatce */
extern uint8_t keys_released[KEY_COUNT];/* 1 = puszczony w tej klatce */

/* Odwzorowanie akcji gry → scancode (konfigurowalny przez SETUP) */
extern uint8_t action_key[ACT_COUNT];

/* ---------- Stan joysticka ------------------------------------------------ */
typedef struct {
    int16_t x, y;       /* osie: -128..+127 po kalibracji */
    uint8_t btn1, btn2; /* 0/1 */
    uint8_t present;    /* 1 = joystick wykryty */
    /* kalibracja */
    int16_t x_min, x_max, x_center;
    int16_t y_min, y_max, y_center;
    uint16_t dead_zone; /* strefa martwa (wartość surowa) */
} JoystickState;

extern JoystickState joy;

/* ---------- Stan myszy ----------------------------------------------------- */
typedef struct {
    int16_t x, y;       /* pozycja względna (delta) */
    uint8_t btn_left, btn_right;
    uint8_t present;    /* 1 = mysz wykryta */
    uint16_t com_port;  /* 0x3F8 = COM1, 0x2F8 = COM2 */
} MouseState;

extern MouseState mouse;

/* ---------- API ------------------------------------------------------------ */

/* Inicjalizacja: instaluje ISR klawiatury (INT 9), próbuje joystick i mysz. */
void input_init(void);

/* Zwalnia ISR-y, przywraca oryginalne handlery. */
void input_shutdown(void);

/* Aktualizacja stanu (wywołaj raz na klatkę, po timer waitframe).
   Przenosi keys_held → oblicza keys_pressed i keys_released.
   Odczytuje joystick i mysz. */
void input_update(void);

/* Sprawdza czy akcja gry jest aktualnie trzymana (klawiatura lub joystick). */
int input_held(GameAction act);

/* Sprawdza czy akcja gry została naciśnięta w tej klatce. */
int input_pressed(GameAction act);

/* Kalibracja joysticka — blokuje do zakończenia procedury kalibracji.
   Wyniki zapisuje do joy.x_min/max itd. */
void input_calibrate_joystick(void);

/* Inicjalizacja myszy serial: COM port, 1200 baud, protokół Microsoft Mouse.
   Zwraca 1 jeśli wykryto mysz. */
int input_init_mouse(uint16_t com_port);

/* Dezaktywacja określonego urządzenia wejściowego. */
void input_disable_device(InputDevice dev);
void input_enable_device(InputDevice dev);

#endif /* INPUT_H */
