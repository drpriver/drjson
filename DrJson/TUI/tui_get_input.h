#ifndef TUI_GET_INPUT_H
#define TUI_GET_INPUT_H

#ifdef _WIN32
#define WIN32_EXTRA_LEAN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
typedef long long ssize_t;
#else
#include <termios.h>
#endif
typedef struct TermState TermState;
struct TermState {
#ifdef _WIN32
    HANDLE STDIN, STDOUT;
#else
    struct termios raw;
    struct termios orig;
#endif
};
static void disable_raw(TermState*);
static void enable_raw(TermState*);

enum {
    KMOD_NONE = 0x0,
    KMOD_SHIFT = 0x1,
    KMOD_CTRL = 0x2,
    KMOD_ALT = 0x4,
};

static
int
get_input(TermState*, int* needs_rescale, int* pc, int* pcx, int* pcy, int* pmagnitude, int* kmod);

enum {
    CTRL_A = 1,         // Ctrl-a
    CTRL_B = 2,         // Ctrl-b
    CTRL_C = 3,         // Ctrl-c
    CTRL_D = 4,         // Ctrl-d
    CTRL_E = 5,         // Ctrl-e
    CTRL_F = 6,         // Ctrl-f
    // CTRL_G = 7,      // unused
    CTRL_H = 8,         // Ctrl-h
    TAB    = 9,         // Tab / Ctrl-i
    CTRL_J = 10,        // Accept
    CTRL_K = 11,        // Ctrl-k
    CTRL_L = 12,        // Ctrl-l
    ENTER = 13,         // Enter / Ctrl-m
    CTRL_N = 14,        // Ctrl-n
    CTRL_O = 15,        // Ctrl-o
    CTRL_P = 16,        // Ctrl-p
    // CTRL_Q = 17,
    CTRL_R = 18,
    // CTRL_S = 19,
    CTRL_T = 20,        // Ctrl-t
    CTRL_U = 21,        // Ctrl-u
    CTRL_V = 22,        // Ctrl-v
    CTRL_W = 23,        // Ctrl-w
    // CTRL_X = 24,
    // CTRL_Y = 25,
    CTRL_Z = 26,        // Ctrl-z
    ESC = 27,           // Escape
    BACKSPACE =  127,   // Backspace
    // fake key codes
    #undef DELETE
    DELETE    = -1,
    UP        = -2,
    DOWN      = -3,
    LEFT      = -4,
    RIGHT     = -5,
    HOME      = -6,
    END       = -7,
    SHIFT_TAB = -8,
    PAGE_UP   = -9,
    PAGE_DOWN = -10,
    LCLICK_DOWN = -11,
    LCLICK_UP = -12,
    LDRAG = -13,
    F1 = -14,
    F2 = -15,
    F3 = -16,
    F4 = -17,
    INSERT = -18,
};

#endif
