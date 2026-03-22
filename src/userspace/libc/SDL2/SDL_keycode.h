#ifndef SDL_keycode_h_
#define SDL_keycode_h_

#include "SDL_scancode.h"
#include "SDL_stdinc.h"

typedef Sint32 SDL_Keycode;

/* SDL keycodes — ASCII range maps directly; special keys use SDLK_SCANCODE_MASK. */
#define SDLK_SCANCODE_MASK (1 << 30)
#define SDL_SCANCODE_TO_KEYCODE(sc) ((sc) | SDLK_SCANCODE_MASK)

#define SDLK_UNKNOWN    0
#define SDLK_RETURN     '\r'
#define SDLK_ESCAPE     27
#define SDLK_BACKSPACE  '\b'
#define SDLK_TAB        '\t'
#define SDLK_SPACE      ' '
#define SDLK_0          '0'
#define SDLK_1          '1'
#define SDLK_2          '2'
#define SDLK_3          '3'
#define SDLK_4          '4'
#define SDLK_5          '5'
#define SDLK_6          '6'
#define SDLK_7          '7'
#define SDLK_8          '8'
#define SDLK_9          '9'
#define SDLK_a          'a'
#define SDLK_b          'b'
#define SDLK_c          'c'
#define SDLK_d          'd'
#define SDLK_e          'e'
#define SDLK_f          'f'
#define SDLK_g          'g'
#define SDLK_h          'h'
#define SDLK_i          'i'
#define SDLK_j          'j'
#define SDLK_k          'k'
#define SDLK_l          'l'
#define SDLK_m          'm'
#define SDLK_n          'n'
#define SDLK_o          'o'
#define SDLK_p          'p'
#define SDLK_q          'q'
#define SDLK_r          'r'
#define SDLK_s          's'
#define SDLK_t          't'
#define SDLK_u          'u'
#define SDLK_v          'v'
#define SDLK_w          'w'
#define SDLK_x          'x'
#define SDLK_y          'y'
#define SDLK_z          'z'

#define SDLK_CAPSLOCK   SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_CAPSLOCK)
#define SDLK_F1         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F1)
#define SDLK_F2         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F2)
#define SDLK_F3         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F3)
#define SDLK_F4         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F4)
#define SDLK_F5         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F5)
#define SDLK_F6         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F6)
#define SDLK_F7         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F7)
#define SDLK_F8         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F8)
#define SDLK_F9         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F9)
#define SDLK_F10        SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F10)
#define SDLK_F11        SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F11)
#define SDLK_F12        SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F12)
#define SDLK_RIGHT      SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RIGHT)
#define SDLK_LEFT       SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LEFT)
#define SDLK_DOWN       SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_DOWN)
#define SDLK_UP         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_UP)
#define SDLK_LCTRL      SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LCTRL)
#define SDLK_LSHIFT     SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LSHIFT)
#define SDLK_LALT       SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LALT)
#define SDLK_RCTRL      SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RCTRL)
#define SDLK_RSHIFT     SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RSHIFT)
#define SDLK_RALT       SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RALT)
#define SDLK_KP_ENTER   SDL_SCANCODE_TO_KEYCODE(88)
#define SDLK_KP_0       SDL_SCANCODE_TO_KEYCODE(98)
#define SDLK_KP_1       SDL_SCANCODE_TO_KEYCODE(89)
#define SDLK_KP_PLUS    SDL_SCANCODE_TO_KEYCODE(87)
#define SDLK_KP_MINUS   SDL_SCANCODE_TO_KEYCODE(86)
#define SDLK_PAUSE       SDL_SCANCODE_TO_KEYCODE(72)
#define SDLK_PRINTSCREEN  SDL_SCANCODE_TO_KEYCODE(70)
#define SDLK_DELETE       127
#define SDLK_PAGEUP       SDL_SCANCODE_TO_KEYCODE(75)
#define SDLK_PAGEDOWN     SDL_SCANCODE_TO_KEYCODE(78)
#define SDLK_HOME         SDL_SCANCODE_TO_KEYCODE(74)
#define SDLK_END          SDL_SCANCODE_TO_KEYCODE(77)
#define SDLK_INSERT       SDL_SCANCODE_TO_KEYCODE(73)
#define SDLK_PLUS         '+'
#define SDLK_MINUS        '-'
#define SDLK_EQUALS       '='
#define SDLK_UNDERSCORE   '_'
#define SDLK_HASH         '#'
#define SDLK_PERIOD       '.'
#define SDLK_COMMA        ','
#define SDLK_COLON        ':'
#define SDLK_SEMICOLON    ';'
#define SDLK_SLASH        '/'
#define SDLK_LESS         '<'
#define SDLK_GREATER      '>'
#define SDLK_QUESTION     '?'
#define SDLK_AT           '@'
#define SDLK_EXCLAIM      '!'
#define SDLK_QUOTEDBL     '"'
#define SDLK_DOLLAR       '$'
#define SDLK_AMPERSAND    '&'
#define SDLK_LEFTPAREN    '('
#define SDLK_RIGHTPAREN   ')'
#define SDLK_ASTERISK     '*'
#define SDLK_CARET        '^'
#define SDLK_BACKQUOTE    '`'
#define SDLK_QUOTE        '\''
#define SDLK_NUMLOCKCLEAR SDL_SCANCODE_TO_KEYCODE(83)
#define SDLK_KP_MULTIPLY  SDL_SCANCODE_TO_KEYCODE(85)
#define SDLK_KP_DIVIDE    SDL_SCANCODE_TO_KEYCODE(84)
#define SDLK_KP_EQUALS    SDL_SCANCODE_TO_KEYCODE(103)
#define SDLK_KP_PERIOD    SDL_SCANCODE_TO_KEYCODE(99)
#define SDLK_LEFTBRACKET  '['
#define SDLK_RIGHTBRACKET ']'
#define SDLK_BACKSLASH    '\\'
#define SDLK_KP_2         SDL_SCANCODE_TO_KEYCODE(90)
#define SDLK_KP_3         SDL_SCANCODE_TO_KEYCODE(91)
#define SDLK_KP_4         SDL_SCANCODE_TO_KEYCODE(92)
#define SDLK_KP_5         SDL_SCANCODE_TO_KEYCODE(93)
#define SDLK_KP_6         SDL_SCANCODE_TO_KEYCODE(94)
#define SDLK_KP_7         SDL_SCANCODE_TO_KEYCODE(95)
#define SDLK_KP_8         SDL_SCANCODE_TO_KEYCODE(96)
#define SDLK_KP_9         SDL_SCANCODE_TO_KEYCODE(97)
#define SDLK_SCROLLLOCK   SDL_SCANCODE_TO_KEYCODE(71)
#define SDLK_LGUI         SDL_SCANCODE_TO_KEYCODE(227)
#define SDLK_RGUI         SDL_SCANCODE_TO_KEYCODE(231)
#define SDLK_F13          SDL_SCANCODE_TO_KEYCODE(104)
#define SDLK_F14          SDL_SCANCODE_TO_KEYCODE(105)
#define SDLK_F15          SDL_SCANCODE_TO_KEYCODE(106)

typedef enum {
    KMOD_NONE   = 0x0000,
    KMOD_LSHIFT = 0x0001,
    KMOD_RSHIFT = 0x0002,
    KMOD_LCTRL  = 0x0040,
    KMOD_RCTRL  = 0x0080,
    KMOD_LALT   = 0x0100,
    KMOD_RALT   = 0x0200,
    KMOD_SHIFT  = KMOD_LSHIFT | KMOD_RSHIFT,
    KMOD_CTRL   = KMOD_LCTRL  | KMOD_RCTRL,
    KMOD_ALT    = KMOD_LALT   | KMOD_RALT,
} SDL_Keymod;

#endif
