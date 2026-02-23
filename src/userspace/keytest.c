#include "libc/stdio.h"
#include "libc/syscall.h"

/* Human-readable names for PS/2 set-1 scancodes (index = scancode). */
static const char *sc_names[128] = {
    /*00*/ "?",        "Esc",     "1",       "2",       "3",       "4",       "5",       "6",
    /*08*/ "7",        "8",       "9",        "0",       "-",       "=",       "BkSp",    "Tab",
    /*10*/ "q",        "w",       "e",        "r",       "t",       "y",       "u",       "i",
    /*18*/ "o",        "p",       "[",        "]",       "Enter",   "LCtrl",   "a",       "s",
    /*20*/ "d",        "f",       "g",        "h",       "j",       "k",       "l",       ";",
    /*28*/ "'",        "`",       "LShift",   "\\",      "z",       "x",       "c",       "v",
    /*30*/ "b",        "n",       "m",        ",",       ".",       "/",       "RShift",  "KP*",
    /*38*/ "LAlt",     "Space",   "CapsLk",   "F1",      "F2",      "F3",      "F4",      "F5",
    /*40*/ "F6",       "F7",      "F8",       "F9",      "F10",     "NumLk",   "ScrLk",   "KP7",
    /*48*/ "Up",       "KP9",     "KP-",      "Left",    "KP5",     "Right",   "KP+",     "KP1",
    /*50*/ "Down",     "KP3",     "KP0",      "KP.",     "?",       "?",       "?",       "F11",
    /*58*/ "F12",      "?",       "?",        "?",       "?",       "?",       "?",       "?",
    /*60*/ "?",        "?",       "?",        "?",       "?",       "?",       "?",       "?",
    /*68*/ "?",        "?",       "?",        "?",       "?",       "?",       "?",       "?",
    /*70*/ "?",        "?",       "?",        "?",       "?",       "?",       "?",       "?",
    /*78*/ "?",        "?",       "?",        "?",       "?",       "?",       "?",       "?",
};

int main(void)
{
    puts("=== key event test ===");
    puts("Press keys; press Esc twice to quit.");
    puts("");

    int esc_count = 0;

    while (1) {
        long ev = sys_get_key_event();
        if (ev == -1L) {
            sys_yield();
            continue;
        }

        int pressed  = (ev & 0x100) != 0;
        int scancode = (int)(ev & 0x7F);
        const char *name = (scancode < 128) ? sc_names[scancode] : "?";

        printf("sc=0x%02x  %-8s  %s\n",
               scancode,
               name,
               pressed ? "PRESS" : "release");

        /* Quit after Esc pressed twice */
        if (scancode == 0x01 && pressed) {
            esc_count++;
            if (esc_count >= 2) break;
        }
    }

    puts("=== done ===");
    return 0;
}
