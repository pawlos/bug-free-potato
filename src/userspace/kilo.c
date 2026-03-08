/* Kilo -- A very simple editor in less than 1-kilo lines of code (as counted
 *         by "cloc"). Does not depend on libcurses, directly emits VT100
 *         escapes on the terminal.
 *
 * -----------------------------------------------------------------------
 *
 * Copyright (C) 2016 Salvatore Sanfilippo <antirez at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * -----------------------------------------------------------------------
 *
 * Ported to potat OS by pawlos + Claude, 2026.
 */

#define KILO_VERSION "0.0.1-potato"

/* potatOS: replaced POSIX headers with potatOS libc equivalents.
 * Original: <termios.h>, <stdlib.h>, <stdio.h>, <stdint.h>, <errno.h>,
 *           <string.h>, <ctype.h>, <sys/types.h>, <sys/ioctl.h>,
 *           <sys/time.h>, <unistd.h>, <fcntl.h>, <signal.h> */
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/ctype.h"
#include "libc/syscall.h"
#include "libc/time.h"
#include "libc/file.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* potatOS: glyph dimensions for computing terminal size from framebuffer pixels */
#define GLYPH_W 8
#define GLYPH_H 16

/* potatOS: PS/2 scancode-to-ASCII tables replace POSIX read() from stdin.
 * The original kilo reads raw bytes via termios raw mode; potatOS delivers
 * PS/2 set-1 scancodes via sys_get_key_event(). */
/* Unshifted ASCII for each scancode index (0x00..0x58). 0 = no mapping. */
static const char sc_to_ascii[0x59] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, /* 00-0E */
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\r', /* 0F-1C */
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',     /* 1D-29 */
    0,   '\\','z','x','c','v','b','n','m',',','.','/',  0,     /* 2A-36 */
    '*', 0, ' ',                                                 /* 37-39 */
    0,0,0,0,0,0,0,0,0,0, /* 3A-43: F1-F10, skip */
    0,0,0, /* 44-46: NumLock, ScrollLock, Home(num) */
    0,0,0, /* 47-49: KP7,KP8,KP9 — but 47=Home,48=Up,49=PgUp on non-num */
    '-',   /* 4A: KP minus */
    0,0,0, /* 4B-4D: KP4,KP5,KP6 — but 4B=Left,4D=Right */
    '+',   /* 4E: KP plus */
    0,0,0, /* 4F-51: KP1,KP2,KP3 — but 4F=End,50=Down,51=PgDn */
    0,0,   /* 52-53: KP0, KP. — but 53=Del */
    0,0,0, /* 54-56 */
    0,0,   /* 57-58: F11,F12 */
};

static const char sc_to_ascii_shift[0x59] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8,
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\r',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,
    0,0,0,
    '-',
    0,0,0,
    '+',
    0,0,0,
    0,0,
    0,0,0,
    0,0,
};

/* Syntax highlight types */
#define HL_NORMAL 0
#define HL_NONPRINT 1
#define HL_COMMENT 2
#define HL_MLCOMMENT 3
#define HL_KEYWORD1 4
#define HL_KEYWORD2 5
#define HL_STRING 6
#define HL_NUMBER 7
#define HL_MATCH 8

#define HL_HIGHLIGHT_STRINGS (1<<0)
#define HL_HIGHLIGHT_NUMBERS (1<<1)

struct editorSyntax {
    char **filematch;
    char **keywords;
    char singleline_comment_start[2];
    char multiline_comment_start[3];
    char multiline_comment_end[3];
    int flags;
};

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_oc;
} erow;

struct editorConfig {
    int cx,cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    long statusmsg_time;
    struct editorSyntax *syntax;
};

static struct editorConfig E;

enum KEY_ACTION {
    KEY_NULL = 0,
    CTRL_C = 3,
    CTRL_D = 4,
    CTRL_F = 6,
    CTRL_H = 8,
    TAB = 9,
    CTRL_L = 12,
    ENTER = 13,
    CTRL_Q = 17,
    CTRL_S = 19,
    CTRL_U = 21,
    ESC = 27,
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

void editorSetStatusMessage(const char *fmt, ...);

/* =========================== Syntax highlights DB ========================= */

char *C_HL_extensions[] = {".c",".h",".cpp",".hpp",".cc",NULL};
char *C_HL_keywords[] = {
    "auto","break","case","continue","default","do","else","enum",
    "extern","for","goto","if","register","return","sizeof","static",
    "struct","switch","typedef","union","volatile","while","NULL",
    "alignas","alignof","and","and_eq","asm","bitand","bitor","class",
    "compl","constexpr","const_cast","deltype","delete","dynamic_cast",
    "explicit","export","false","friend","inline","mutable","namespace",
    "new","noexcept","not","not_eq","nullptr","operator","or","or_eq",
    "private","protected","public","reinterpret_cast","static_assert",
    "static_cast","template","this","thread_local","throw","true","try",
    "typeid","typename","virtual","xor","xor_eq",
    "int|","long|","double|","float|","char|","unsigned|","signed|",
    "void|","short|","auto|","const|","bool|",NULL
};

struct editorSyntax HLDB[] = {
    {
        C_HL_extensions,
        C_HL_keywords,
        "//","/*","*/",
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
    }
};

#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))

/* ======================= Low level terminal handling ====================== */

/* potatOS: entire low-level terminal section rewritten.
 * Original kilo uses termios to enter raw mode and reads stdin with read().
 * Removed: enableRawMode(), disableRawMode(), atexit(disableRawMode),
 *          getCursorPosition(), getWindowSize(), SIGWINCH handler.
 * potatOS terminals are always in raw mode — no termios needed. */

static void term_write(const char *buf, int len) {
    sys_write(1, buf, len);
}

/* potatOS: completely rewritten from original editorReadKey().
 * Original used read(STDIN_FILENO, &c, 1) + escape sequence parsing.
 * This version uses sys_get_key_event() returning PS/2 set-1 scancodes,
 * with manual shift/ctrl tracking and scancode-to-ASCII translation. */
static int editorReadKey(void) {
    static int ctrl_held  = 0;
    static int shift_held = 0;

    for (;;) {
        long ev = sys_get_key_event();
        if (ev == -1) {
            sys_yield();
            continue;
        }

        int pressed  = (ev & 0x100) != 0;
        int scancode = ev & 0xFF;

        /* Track modifier state */
        if (scancode == 0x1D) { ctrl_held  = pressed; continue; }
        if (scancode == 0x2A || scancode == 0x36) { shift_held = pressed; continue; }
        if (scancode == 0x38) { continue; } /* Alt — ignore */

        /* Only process key-press events */
        if (!pressed) continue;

        /* Special keys */
        switch (scancode) {
        case 0x48: return ARROW_UP;
        case 0x50: return ARROW_DOWN;
        case 0x4B: return ARROW_LEFT;
        case 0x4D: return ARROW_RIGHT;
        case 0x47: return HOME_KEY;
        case 0x4F: return END_KEY;
        case 0x49: return PAGE_UP;
        case 0x51: return PAGE_DOWN;
        case 0x53: return DEL_KEY;
        case 0x01: return ESC;
        case 0x0E: return BACKSPACE;
        case 0x1C: return ENTER;
        case 0x0F: return TAB;
        }

        /* Regular ASCII keys */
        if (scancode < 0x59) {
            char ch;
            if (shift_held)
                ch = sc_to_ascii_shift[scancode];
            else
                ch = sc_to_ascii[scancode];

            if (ch == 0) continue; /* unmapped */

            if (ctrl_held && ch >= 'a' && ch <= 'z')
                return ch - 'a' + 1; /* Ctrl+letter → 1..26 */
            if (ctrl_held && ch >= 'A' && ch <= 'Z')
                return ch - 'A' + 1;

            return ch;
        }
    }
}

/* ====================== Syntax highlight color scheme ==================== */

static int is_separator(int c) {
    return c == '\0' || isspace(c) || strchr(",.()+-/*=~%[];",c) != NULL;
}

static int editorRowHasOpenComment(erow *row) {
    if (row->hl && row->rsize && row->hl[row->rsize-1] == HL_MLCOMMENT &&
        (row->rsize < 2 || (row->render[row->rsize-2] != '*' ||
                            row->render[row->rsize-1] != '/'))) return 1;
    return 0;
}

static void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl,row->rsize);
    memset(row->hl,HL_NORMAL,row->rsize);

    if (E.syntax == NULL) return;

    int i, prev_sep, in_string, in_comment;
    char *p;
    char **keywords = E.syntax->keywords;
    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    p = row->render;
    i = 0;
    while(*p && isspace(*p)) { p++; i++; }
    prev_sep = 1;
    in_string = 0;
    in_comment = 0;

    if (row->idx > 0 && editorRowHasOpenComment(&E.row[row->idx-1]))
        in_comment = 1;

    while(*p) {
        if (prev_sep && *p == scs[0] && *(p+1) == scs[1]) {
            memset(row->hl+i,HL_COMMENT,row->size-i);
            return;
        }

        if (in_comment) {
            row->hl[i] = HL_MLCOMMENT;
            if (*p == mce[0] && *(p+1) == mce[1]) {
                row->hl[i+1] = HL_MLCOMMENT;
                p += 2; i += 2;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                prev_sep = 0;
                p++; i++;
                continue;
            }
        } else if (*p == mcs[0] && *(p+1) == mcs[1]) {
            row->hl[i] = HL_MLCOMMENT;
            row->hl[i+1] = HL_MLCOMMENT;
            p += 2; i += 2;
            in_comment = 1;
            prev_sep = 0;
            continue;
        }

        if (in_string) {
            row->hl[i] = HL_STRING;
            if (*p == '\\') {
                row->hl[i+1] = HL_STRING;
                p += 2; i += 2;
                prev_sep = 0;
                continue;
            }
            if (*p == in_string) in_string = 0;
            p++; i++;
            continue;
        } else {
            if (*p == '"' || *p == '\'') {
                in_string = *p;
                row->hl[i] = HL_STRING;
                p++; i++;
                prev_sep = 0;
                continue;
            }
        }

        if (!isprint(*p)) {
            row->hl[i] = HL_NONPRINT;
            p++; i++;
            prev_sep = 0;
            continue;
        }

        if ((isdigit(*p) && (prev_sep || row->hl[i-1] == HL_NUMBER)) ||
            (*p == '.' && i > 0 && row->hl[i-1] == HL_NUMBER)) {
            row->hl[i] = HL_NUMBER;
            p++; i++;
            prev_sep = 0;
            continue;
        }

        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen-1] == '|';
                if (kw2) klen--;

                if (!memcmp(p,keywords[j],klen) &&
                    is_separator(*(p+klen)))
                {
                    memset(row->hl+i,kw2 ? HL_KEYWORD2 : HL_KEYWORD1,klen);
                    p += klen;
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(*p);
        p++; i++;
    }

    int oc = editorRowHasOpenComment(row);
    if (row->hl_oc != oc && row->idx+1 < E.numrows)
        editorUpdateSyntax(&E.row[row->idx+1]);
    row->hl_oc = oc;
}

static int editorSyntaxToColor(int hl) {
    switch(hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;  /* cyan */
    case HL_KEYWORD1: return 33;   /* yellow */
    case HL_KEYWORD2: return 32;   /* green */
    case HL_STRING: return 35;     /* magenta */
    case HL_NUMBER: return 31;     /* red */
    case HL_MATCH: return 34;      /* blue */
    default: return 37;            /* white */
    }
}

static void editorSelectSyntaxHighlight(char *filename) {
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = HLDB+j;
        unsigned int i = 0;
        while(s->filematch[i]) {
            char *p;
            int patlen = strlen(s->filematch[i]);
            if ((p = strstr(filename,s->filematch[i])) != NULL) {
                if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
                    E.syntax = s;
                    return;
                }
            }
            i++;
        }
    }
}

/* ======================= Editor rows implementation ======================= */

static void editorUpdateRow(erow *row) {
    unsigned int tabs = 0;
    int j, idx;

    free(row->render);
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == TAB) tabs++;

    row->render = malloc(row->size + tabs*8 + 1);
    idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == TAB) {
            row->render[idx++] = ' ';
            while((idx+1) % 8 != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->rsize = idx;
    row->render[idx] = '\0';

    editorUpdateSyntax(row);
}

static void editorInsertRow(int at, char *s, int len) {
    if (at > E.numrows) return;
    E.row = realloc(E.row,sizeof(erow)*(E.numrows+1));
    if (at != E.numrows) {
        memmove(E.row+at+1,E.row+at,sizeof(E.row[0])*(E.numrows-at));
        for (int j = at+1; j <= E.numrows; j++) E.row[j].idx++;
    }
    E.row[at].size = len;
    E.row[at].chars = malloc(len+1);
    memcpy(E.row[at].chars,s,len+1);
    E.row[at].hl = NULL;
    E.row[at].hl_oc = 0;
    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    E.row[at].idx = at;
    editorUpdateRow(E.row+at);
    E.numrows++;
    E.dirty++;
}

static void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

static void editorDelRow(int at) {
    erow *row;
    if (at >= E.numrows) return;
    row = E.row+at;
    editorFreeRow(row);
    memmove(E.row+at,E.row+at+1,sizeof(E.row[0])*(E.numrows-at-1));
    for (int j = at; j < E.numrows-1; j++) E.row[j].idx = j;
    E.numrows--;
    E.dirty++;
}

static char *editorRowsToString(int *buflen) {
    char *buf = NULL, *p;
    int totlen = 0;
    int j;

    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size+1;
    *buflen = totlen;
    totlen++;

    p = buf = malloc(totlen);
    for (j = 0; j < E.numrows; j++) {
        memcpy(p,E.row[j].chars,E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    *p = '\0';
    return buf;
}

static void editorRowInsertChar(erow *row, int at, int c) {
    if (at > row->size) {
        int padlen = at-row->size;
        row->chars = realloc(row->chars,row->size+padlen+2);
        memset(row->chars+row->size,' ',padlen);
        row->chars[row->size+padlen+1] = '\0';
        row->size += padlen+1;
    } else {
        row->chars = realloc(row->chars,row->size+2);
        memmove(row->chars+at+1,row->chars+at,row->size-at+1);
        row->size++;
    }
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

static void editorRowAppendString(erow *row, char *s, int len) {
    row->chars = realloc(row->chars,row->size+len+1);
    memcpy(row->chars+row->size,s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

static void editorRowDelChar(erow *row, int at) {
    if (row->size <= at) return;
    memmove(row->chars+at,row->chars+at+1,row->size-at);
    editorUpdateRow(row);
    row->size--;
    E.dirty++;
}

static void editorInsertChar(int c) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row) {
        while(E.numrows <= filerow)
            editorInsertRow(E.numrows,"",0);
    }
    row = &E.row[filerow];
    editorRowInsertChar(row,filecol,c);
    if (E.cx == E.screencols-1)
        E.coloff++;
    else
        E.cx++;
    E.dirty++;
}

static void editorInsertNewline(void) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row) {
        if (filerow == E.numrows) {
            editorInsertRow(filerow,"",0);
            goto fixcursor;
        }
        return;
    }
    if (filecol >= row->size) filecol = row->size;
    if (filecol == 0) {
        editorInsertRow(filerow,"",0);
    } else {
        editorInsertRow(filerow+1,row->chars+filecol,row->size-filecol);
        row = &E.row[filerow];
        row->chars[filecol] = '\0';
        row->size = filecol;
        editorUpdateRow(row);
    }
fixcursor:
    if (E.cy == E.screenrows-1) {
        E.rowoff++;
    } else {
        E.cy++;
    }
    E.cx = 0;
    E.coloff = 0;
}

static void editorDelChar(void) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    if (!row || (filecol == 0 && filerow == 0)) return;
    if (filecol == 0) {
        filecol = E.row[filerow-1].size;
        editorRowAppendString(&E.row[filerow-1],row->chars,row->size);
        editorDelRow(filerow);
        row = NULL;
        if (E.cy == 0)
            E.rowoff--;
        else
            E.cy--;
        E.cx = filecol;
        if (E.cx >= E.screencols) {
            int shift = (E.screencols-E.cx)+1;
            E.cx -= shift;
            E.coloff += shift;
        }
    } else {
        editorRowDelChar(row,filecol-1);
        if (E.cx == 0 && E.coloff)
            E.coloff--;
        else
            E.cx--;
    }
    if (row) editorUpdateRow(row);
    E.dirty++;
}

/* potatOS: rewritten file I/O. Original used fopen()/fgets()/fclose().
 * This version uses sys_open()/sys_read()/sys_close() and reads the
 * entire file into a buffer, then splits into lines. */
static int editorOpen(char *filename) {
    E.dirty = 0;
    free(E.filename);
    int fnlen = strlen(filename)+1;
    E.filename = malloc(fnlen);
    memcpy(E.filename,filename,fnlen);

    int fd = sys_open(filename);
    if (fd < 0) return 1; /* new file */

    /* Read entire file into a buffer */
    char tmp[512];
    char *filebuf = NULL;
    int filelen = 0;
    long n;
    while ((n = sys_read(fd, tmp, sizeof(tmp))) > 0) {
        filebuf = realloc(filebuf, filelen + n + 1);
        memcpy(filebuf + filelen, tmp, n);
        filelen += n;
    }
    sys_close(fd);

    if (filebuf) {
        filebuf[filelen] = '\0';
        /* Parse lines */
        char *line = filebuf;
        for (int i = 0; i <= filelen; i++) {
            if (i == filelen || filebuf[i] == '\n') {
                int linelen = &filebuf[i] - line;
                /* Strip trailing \r */
                if (linelen > 0 && line[linelen-1] == '\r')
                    linelen--;
                editorInsertRow(E.numrows, line, linelen);
                line = &filebuf[i+1];
            }
        }
        free(filebuf);
    }

    E.dirty = 0;
    return 0;
}

/* potatOS: rewritten file save. Original used open()/write()/close() with
 * O_CREAT|O_WRONLY|O_TRUNC. Uses sys_create()/sys_write()/sys_close(). */
static int editorSave(void) {
    int len;
    char *buf = editorRowsToString(&len);

    /* sys_create truncates the file, then we write the content */
    int fd = sys_create(E.filename);
    if (fd < 0) {
        free(buf);
        editorSetStatusMessage("Can't save! I/O error");
        return 1;
    }

    long written = sys_write(fd, buf, len);
    sys_close(fd);
    free(buf);

    if (written != len) {
        editorSetStatusMessage("Can't save! I/O error (short write)");
        return 1;
    }

    E.dirty = 0;
    editorSetStatusMessage("%d bytes written on disk", len);
    return 0;
}

/* ============================= Terminal update ============================ */

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL,0}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *p = realloc(ab->b, ab->len+len);
    if (p == NULL) return;
    memcpy(p+ab->len,s,len);
    ab->b = p;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

static void editorRefreshScreen(void) {
    int y;
    erow *r;
    char buf[32];
    struct abuf ab = ABUF_INIT;

    abAppend(&ab,"\x1b[?25l",6); /* Hide cursor */
    abAppend(&ab,"\x1b[H",3);    /* Go home */
    for (y = 0; y < E.screenrows; y++) {
        int filerow = E.rowoff+y;

        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows/3) {
                char welcome[80];
                int welcomelen = snprintf(welcome,sizeof(welcome),
                    "Kilo editor -- version %s\x1b[0K\r\n", KILO_VERSION);
                int padding = (E.screencols-welcomelen)/2;
                if (padding) {
                    abAppend(&ab,"~",1);
                    padding--;
                }
                while(padding-- > 0) abAppend(&ab," ",1);
                abAppend(&ab,welcome,welcomelen);
            } else {
                abAppend(&ab,"~\x1b[0K\r\n",7);
            }
            continue;
        }

        r = &E.row[filerow];

        int len = r->rsize - E.coloff;
        int current_color = -1;
        if (len > 0) {
            if (len > E.screencols) len = E.screencols;
            char *c = r->render+E.coloff;
            unsigned char *hl = r->hl+E.coloff;
            int j;
            for (j = 0; j < len; j++) {
                if (hl[j] == HL_NONPRINT) {
                    char sym;
                    abAppend(&ab,"\x1b[7m",4);
                    if (c[j] <= 26)
                        sym = '@'+c[j];
                    else
                        sym = '?';
                    abAppend(&ab,&sym,1);
                    abAppend(&ab,"\x1b[0m",4);
                } else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        abAppend(&ab,"\x1b[39m",5);
                        current_color = -1;
                    }
                    abAppend(&ab,c+j,1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        char cbuf[16];
                        int clen = snprintf(cbuf,sizeof(cbuf),"\x1b[%dm",color);
                        current_color = color;
                        abAppend(&ab,cbuf,clen);
                    }
                    abAppend(&ab,c+j,1);
                }
            }
        }
        abAppend(&ab,"\x1b[39m",5);
        abAppend(&ab,"\x1b[0K",4);
        abAppend(&ab,"\r\n",2);
    }

    /* Status bar (inverted) */
    abAppend(&ab,"\x1b[0K",4);
    abAppend(&ab,"\x1b[7m",4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        E.filename, E.numrows, E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus),
        "%d/%d",E.rowoff+E.cy+1,E.numrows);
    if (len > E.screencols) len = E.screencols;
    abAppend(&ab,status,len);
    while(len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(&ab,rstatus,rlen);
            break;
        } else {
            abAppend(&ab," ",1);
            len++;
        }
    }
    abAppend(&ab,"\x1b[0m\r\n",6);

    /* Message bar */
    abAppend(&ab,"\x1b[0K",4);
    int msglen = strlen(E.statusmsg);
    /* potatOS: extended from 5 to 30 seconds (help text disappears too fast) */
    if (msglen && time(NULL)-E.statusmsg_time < 30)
        abAppend(&ab,E.statusmsg,msglen <= E.screencols ? msglen : E.screencols);

    /* Position cursor */
    int j;
    int cx = 1;
    int filerow = E.rowoff+E.cy;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    if (row) {
        for (j = E.coloff; j < (E.cx+E.coloff); j++) {
            if (j < row->size && row->chars[j] == TAB) cx += 7-((cx)%8);
            cx++;
        }
    }
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy+1,cx);
    abAppend(&ab,buf,strlen(buf));
    abAppend(&ab,"\x1b[?25h",6); /* Show cursor */
    term_write(ab.b,ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/* =============================== Find mode ================================ */

#define KILO_QUERY_LEN 256

static void editorFind(void) {
    char query[KILO_QUERY_LEN+1] = {0};
    int qlen = 0;
    int last_match = -1;
    int find_next = 0;
    int saved_hl_line = -1;
    char *saved_hl = NULL;

#define FIND_RESTORE_HL do { \
    if (saved_hl) { \
        memcpy(E.row[saved_hl_line].hl,saved_hl, E.row[saved_hl_line].rsize); \
        free(saved_hl); \
        saved_hl = NULL; \
    } \
} while (0)

    int saved_cx = E.cx, saved_cy = E.cy;
    int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

    while(1) {
        editorSetStatusMessage(
            "Search: %s (Use ESC/Arrows/Enter)", query);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
            if (qlen != 0) query[--qlen] = '\0';
            last_match = -1;
        } else if (c == ESC || c == ENTER) {
            if (c == ESC) {
                E.cx = saved_cx; E.cy = saved_cy;
                E.coloff = saved_coloff; E.rowoff = saved_rowoff;
            }
            FIND_RESTORE_HL;
            editorSetStatusMessage("");
            return;
        } else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
            find_next = 1;
        } else if (c == ARROW_LEFT || c == ARROW_UP) {
            find_next = -1;
        } else if (isprint(c)) {
            if (qlen < KILO_QUERY_LEN) {
                query[qlen++] = c;
                query[qlen] = '\0';
                last_match = -1;
            }
        }

        if (last_match == -1) find_next = 1;
        if (find_next) {
            char *match = NULL;
            int match_offset = 0;
            int i, current = last_match;

            for (i = 0; i < E.numrows; i++) {
                current += find_next;
                if (current == -1) current = E.numrows-1;
                else if (current == E.numrows) current = 0;
                match = strstr(E.row[current].render,query);
                if (match) {
                    match_offset = match-E.row[current].render;
                    break;
                }
            }
            find_next = 0;

            FIND_RESTORE_HL;

            if (match) {
                erow *row = &E.row[current];
                last_match = current;
                if (row->hl) {
                    saved_hl_line = current;
                    saved_hl = malloc(row->rsize);
                    memcpy(saved_hl,row->hl,row->rsize);
                    memset(row->hl+match_offset,HL_MATCH,qlen);
                }
                E.cy = 0;
                E.cx = match_offset;
                E.rowoff = current;
                E.coloff = 0;
                if (E.cx > E.screencols) {
                    int diff = E.cx - E.screencols;
                    E.cx -= diff;
                    E.coloff += diff;
                }
            }
        }
    }
}

/* ========================= Editor events handling ========================= */

static void editorMoveCursor(int key) {
    int filerow = E.rowoff+E.cy;
    int filecol = E.coloff+E.cx;
    int rowlen;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    switch(key) {
    case ARROW_LEFT:
        if (E.cx == 0) {
            if (E.coloff) {
                E.coloff--;
            } else {
                if (filerow > 0) {
                    E.cy--;
                    E.cx = E.row[filerow-1].size;
                    if (E.cx > E.screencols-1) {
                        E.coloff = E.cx-E.screencols+1;
                        E.cx = E.screencols-1;
                    }
                }
            }
        } else {
            E.cx -= 1;
        }
        break;
    case ARROW_RIGHT:
        if (row && filecol < row->size) {
            if (E.cx == E.screencols-1) {
                E.coloff++;
            } else {
                E.cx += 1;
            }
        } else if (row && filecol == row->size) {
            E.cx = 0;
            E.coloff = 0;
            if (E.cy == E.screenrows-1) {
                E.rowoff++;
            } else {
                E.cy += 1;
            }
        }
        break;
    case ARROW_UP:
        if (E.cy == 0) {
            if (E.rowoff) E.rowoff--;
        } else {
            E.cy -= 1;
        }
        break;
    case ARROW_DOWN:
        if (filerow < E.numrows) {
            if (E.cy == E.screenrows-1) {
                E.rowoff++;
            } else {
                E.cy += 1;
            }
        }
        break;
    }
    filerow = E.rowoff+E.cy;
    filecol = E.coloff+E.cx;
    row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    rowlen = row ? row->size : 0;
    if (filecol > rowlen) {
        E.cx -= filecol-rowlen;
        if (E.cx < 0) {
            E.coloff += E.cx;
            E.cx = 0;
        }
    }
}

#define KILO_QUIT_TIMES 3

static void editorProcessKeypress(void) {
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();
    switch(c) {
    case ENTER:
        editorInsertNewline();
        break;
    case CTRL_C:
        break;
    case CTRL_Q:
        if (E.dirty && quit_times) {
            editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                "Press Ctrl-Q %d more times to quit.", quit_times);
            quit_times--;
            return;
        }
        /* potatOS: clear screen + sys_exit() instead of write()+exit() */
        term_write("\x1b[2J\x1b[H", 7);
        sys_exit(0);
        break;
    case CTRL_S:
        editorSave();
        break;
    case CTRL_F:
        editorFind();
        break;
    case BACKSPACE:
    case CTRL_H:
    case DEL_KEY:
        editorDelChar();
        break;
    case PAGE_UP:
    case PAGE_DOWN:
        if (c == PAGE_UP && E.cy != 0)
            E.cy = 0;
        else if (c == PAGE_DOWN && E.cy != E.screenrows-1)
            E.cy = E.screenrows-1;
        {
            int times = E.screenrows;
            while(times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
    case CTRL_L:
        break;
    case ESC:
        break;
    default:
        editorInsertChar(c);
        break;
    }

    quit_times = KILO_QUIT_TIMES;
}

static void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.syntax = NULL;
    /* potatOS: replaced ioctl(TIOCGWINSZ) / getWindowSize() with
     * framebuffer pixel dimensions divided by glyph size. */
    int cols = (int)(sys_fb_width()  / GLYPH_W);
    int rows = (int)(sys_fb_height() / GLYPH_H);
    if (cols <= 0) cols = 80;
    if (rows <= 0) rows = 50;
    E.screenrows = rows - 2; /* room for status bar + message */
    E.screencols = cols;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        const char *usage = "Usage: kilo <filename>\n";
        sys_write(1, usage, strlen(usage));
        sys_exit(1);
    }

    initEditor();
    /* potatOS: clear window on startup (original kilo doesn't need this
     * because the terminal is already clean after entering raw mode). */
    term_write("\x1b[2J\x1b[H", 7);
    editorSelectSyntaxHighlight(argv[1]);
    editorOpen(argv[1]);
    editorSetStatusMessage(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
