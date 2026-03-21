#pragma once

/* _IS* classification bits — needed by GCC's C++ <bits/ctype_base.h>.
   Layout matches glibc x86_64 (big-endian bit order). */
#ifndef _ISbit
# define _ISbit(bit) ((bit) < 8 ? ((1 << (bit)) << 8) : ((1 << (bit)) >> 8))
#endif

enum {
    _ISupper = _ISbit(0),
    _ISlower = _ISbit(1),
    _ISalpha = _ISbit(2),
    _ISdigit = _ISbit(3),
    _ISxdigit = _ISbit(4),
    _ISspace = _ISbit(5),
    _ISprint = _ISbit(6),
    _ISgraph = _ISbit(7),
    _ISblank = _ISbit(8),
    _IScntrl = _ISbit(9),
    _ISpunct = _ISbit(10),
    _ISalnum = _ISbit(11),
};

static inline int isdigit (int c) { return c >= '0' && c <= '9'; }
static inline int isxdigit(int c) { return isdigit(c) || (c>='a'&&c<='f') || (c>='A'&&c<='F'); }
static inline int islower (int c) { return c >= 'a' && c <= 'z'; }
static inline int isupper (int c) { return c >= 'A' && c <= 'Z'; }
static inline int isalpha (int c) { return islower(c) || isupper(c); }
static inline int isalnum (int c) { return isalpha(c) || isdigit(c); }
static inline int isspace (int c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
static inline int ispunct (int c) { return (c>='!'&&c<='/') || (c>=':'&&c<='@') || (c>='['&&c<='`') || (c>='{'&&c<='~'); }
static inline int isgraph (int c) { return c > 0x20 && c < 0x7f; }
static inline int isprint (int c) { return c >= 0x20 && c < 0x7f; }
static inline int iscntrl (int c) { return (unsigned)c < 0x20 || c == 0x7f; }
static inline int isblank (int c) { return c == ' ' || c == '\t'; }
static inline int toupper (int c) { return islower(c) ? c - ('a'-'A') : c; }
static inline int tolower (int c) { return isupper(c) ? c + ('a'-'A') : c; }
