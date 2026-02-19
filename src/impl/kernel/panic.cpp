#include "kernel.h"
#include "framebuffer.h"
#include "fbterm.h"
#include "print.h"
#include "elf.h"
#include "boot.h"
#include "io.h"

// ── Global ELF state ─────────────────────────────────────────────────────────

static const boot_elf_symbols* g_elf_tag = nullptr;

void panic_set_elf_symbols(const boot_elf_symbols* tag) {
    g_elf_tag = tag;
}

// ── Minimal string helpers (no heap) ─────────────────────────────────────────

static char* w_hex64(char* p, pt::uint64_t v) {
    const char h[] = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) *p++ = h[(v >> i) & 0xF];
    return p;
}

static char* w_dec(char* p, pt::uint64_t v) {
    if (v == 0) { *p++ = '0'; return p; }
    char tmp[20];
    int n = 0;
    while (v) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    for (int i = n - 1; i >= 0; i--) *p++ = tmp[i];
    return p;
}

static char* w_str(char* p, const char* s) {
    while (*s) *p++ = *s++;
    return p;
}

static char* w_pad2(char* p, int v) {
    *p++ = '0' + v / 10;
    *p++ = '0' + v % 10;
    return p;
}

// ── ELF symbol lookup ─────────────────────────────────────────────────────────

static const char* lookup_symbol(pt::uint64_t addr, pt::uint64_t* offset_out) {
    if (!g_elf_tag) return nullptr;

    const auto* shdrs = reinterpret_cast<const Elf64_Shdr*>(
        reinterpret_cast<const pt::uint8_t*>(g_elf_tag) + sizeof(boot_elf_symbols));
    const pt::uint16_t num = g_elf_tag->num;

    const Elf64_Sym* symtab = nullptr;
    pt::uint64_t     sym_count = 0;
    const char*      strtab = nullptr;

    for (pt::uint16_t i = 0; i < num; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            symtab    = reinterpret_cast<const Elf64_Sym*>(
                            static_cast<pt::uintptr_t>(shdrs[i].sh_addr));
            sym_count = shdrs[i].sh_size / sizeof(Elf64_Sym);
            const pt::uint16_t si = static_cast<pt::uint16_t>(shdrs[i].sh_link);
            if (si < num)
                strtab = reinterpret_cast<const char*>(
                            static_cast<pt::uintptr_t>(shdrs[si].sh_addr));
            break;
        }
    }

    if (!symtab || !strtab) return nullptr;

    // Pass 1: exact range match (STT_FUNC, st_size > 0)
    for (pt::uint64_t i = 0; i < sym_count; i++) {
        const Elf64_Sym& sym = symtab[i];
        if ((sym.st_info & 0xF) != STT_FUNC) continue;
        if (sym.st_size == 0) continue;
        if (addr >= sym.st_value && addr < sym.st_value + sym.st_size) {
            *offset_out = addr - sym.st_value;
            return strtab + sym.st_name;
        }
    }

    // Pass 2: nearest STT_FUNC with st_value <= addr
    const Elf64_Sym* best = nullptr;
    for (pt::uint64_t i = 0; i < sym_count; i++) {
        const Elf64_Sym& sym = symtab[i];
        if ((sym.st_info & 0xF) != STT_FUNC) continue;
        if (sym.st_value > addr) continue;
        if (!best || sym.st_value > best->st_value) best = &symtab[i];
    }

    if (best) {
        *offset_out = addr - best->st_value;
        return strtab + best->st_name;
    }

    return nullptr;
}

// ── Minimal C++ demangler ────────────────────────────────────────────────────

static char dm_buf[256];
static char* dm_out;
static const char* dm_lim;

static void dm_s(const char* s) {
    while (*s && dm_out < dm_lim) *dm_out++ = *s++;
}

static void dm_sn(const char* s, int n) {
    while (n-- > 0 && dm_out < dm_lim) *dm_out++ = *s++;
}

static void parse_type(const char*& s);

static void parse_type(const char*& s) {
    if (!*s) return;
    switch (*s) {
        case 'P': s++; parse_type(s); dm_s("*");         return;
        case 'K': s++; dm_s("const "); parse_type(s);    return;
        case 'R': s++; parse_type(s); dm_s("&");         return;
        case 'v': s++; dm_s("void");                     return;
        case 'b': s++; dm_s("bool");                     return;
        case 'c': s++; dm_s("char");                     return;
        case 's': s++; dm_s("short");                    return;
        case 'i': s++; dm_s("int");                      return;
        case 'j': s++; dm_s("unsigned int");             return;
        case 'l': s++; dm_s("long");                     return;
        case 'm': s++; dm_s("unsigned long");            return;
        case 'x': s++; dm_s("long long");               return;
        case 'y': s++; dm_s("unsigned long long");       return;
        case 'f': s++; dm_s("float");                    return;
        case 'd': s++; dm_s("double");                   return;
        case 'I': {
            s++;
            int depth = 1;
            while (*s && depth > 0) {
                if (*s == 'I') depth++;
                else if (*s == 'E') depth--;
                s++;
            }
            return;
        }
        default: {
            if (*s >= '0' && *s <= '9') {
                int len = 0;
                while (*s >= '0' && *s <= '9') len = len * 10 + (*s++ - '0');
                dm_sn(s, len);
                s += len;
                return;
            }
            s++;
            return;
        }
    }
}

static const char* demangle(const char* sym) {
    if (sym[0] != '_' || sym[1] != 'Z') return sym;

    dm_out = dm_buf;
    dm_lim = dm_buf + sizeof(dm_buf) - 1;

    const char* s = sym + 2;
    const char* last_class     = nullptr;
    int         last_class_len = 0;

    if (*s == 'N') {
        s++;
        bool first = true;
        while (*s && *s != 'E') {
            if (*s >= '0' && *s <= '9') {
                int len = 0;
                while (*s >= '0' && *s <= '9') len = len * 10 + (*s++ - '0');
                if (!first) dm_s("::");
                last_class     = s;
                last_class_len = len;
                dm_sn(s, len);
                s += len;
                first = false;
            } else if (*s == 'C') {
                s++;
                if (*s == '1' || *s == '2') s++;
                if (!first) dm_s("::");
                if (last_class) dm_sn(last_class, last_class_len);
                first = false;
            } else if (*s == 'D') {
                s++;
                if (*s == '1' || *s == '2') s++;
                if (!first) dm_s("::~");
                else        dm_s("~");
                if (last_class) dm_sn(last_class, last_class_len);
                first = false;
            } else if (*s == 'I') {
                s++;
                int depth = 1;
                while (*s && depth > 0) {
                    if (*s == 'I') depth++;
                    else if (*s == 'E') depth--;
                    s++;
                }
            } else {
                s++;
            }
        }
        if (*s == 'E') s++;
    } else if (*s >= '0' && *s <= '9') {
        int len = 0;
        while (*s >= '0' && *s <= '9') len = len * 10 + (*s++ - '0');
        dm_sn(s, len);
        s += len;
    }

    // Argument list
    if (*s) {
        dm_s("(");
        bool first = true;
        while (*s) {
            if (!first) dm_s(", ");
            parse_type(s);
            first = false;
        }
        dm_s(")");
    }

    *dm_out = '\0';
    return dm_buf;
}

// ── Error code string table ───────────────────────────────────────────────────

static const char* reason_string(int code) {
    switch (code) {
        case 255: return "holy trinity";
        case 254: return "memory entries limit";
        case 253: return "boot info not parsed";
        case 252: return "no suitable memory region";
        case 251: return "heap exhausted";
        case 250: return "mouse not acked";
        case 249: return "null ref";
        case 248: return "ACPI not available";
        default:  return "unknown";
    }
}

// ── Direct COM1 output (bypasses fbterm to keep the framebuffer clean) ───────

static void com1_ch(char c) {
    while ((IO::inb(COM1 + 5) & 0x20) == 0) {}
    IO::outb(COM1, c);
}

static void com1_str(const char* s) {
    while (*s) com1_ch(*s++);
    com1_ch('\n');
}

// ── Output helpers ────────────────────────────────────────────────────────────

// Write line to COM1 directly (no fbterm) and draw on framebuffer via draw_at.
static void panic_line(Framebuffer* fb, int y, const char* text,
                       pt::uint32_t fg = 0xFFFFFF, pt::uint32_t bg = 0x8B0000) {
    com1_str(text);
    if (fb && fbterm.is_ready())
        fbterm.draw_at(0, (pt::uint32_t)y, text, fg, bg);
}

// Build symbol annotation "  SymName(args)+0xOFF" into buf, return end ptr.
static char* append_sym(char* p, pt::uint64_t addr) {
    pt::uint64_t sym_off = 0;
    const char*  sym_raw = lookup_symbol(addr, &sym_off);
    if (!sym_raw) return p;
    p = w_str(p, "  ");
    p = w_str(p, demangle(sym_raw));
    p = w_str(p, "+0x");
    p = w_hex64(p, sym_off);
    return p;
}

// ── Main panic implementation ─────────────────────────────────────────────────

[[noreturn]] void _kernel_panic_impl(const char* str, int reason,
                                     const PanicRegs& regs, pt::uint64_t /*rbp_hint*/) {
    // Capture our own frame pointer so we can walk the real call stack.
    pt::uint64_t own_rbp;
    asm volatile("mov %0, rbp" : "=r"(own_rbp));

    Framebuffer* fb = Framebuffer::get_instance();

    // ── Background ───────────────────────────────────────────────────────────
    if (fb) {
        fb->Clear(0x8B, 0x00, 0x00);
        // Bright-red title bar
        fb->FillRect(0, 0, fb->get_width(), 16, 0xCC, 0x00, 0x00);
    }

    // ── Title (COM1 + framebuffer) ────────────────────────────────────────────
    com1_ch('\n'); com1_ch('\n');
    com1_str("*** KERNEL PANIC ***");
    if (fb && fbterm.is_ready())
        fbterm.draw_at(0, 0, "*** KERNEL PANIC ***", 0xFFFFFF, 0xCC0000);

    // ── Content lines ────────────────────────────────────────────────────────
    char buf[256];
    char* p;
    int y = 32;   // start below title bar

    // Reason — use the caller's message string
    p = buf;
    p = w_str(p, "Reason : ");
    p = w_str(p, str ? str : reason_string(reason));
    *p = '\0';
    panic_line(fb, y, buf); y += 16;

    // Code
    p = buf;
    p = w_str(p, "Code   : ");
    p = w_dec(p, (pt::uint64_t)(unsigned)reason);
    *p = '\0';
    panic_line(fb, y, buf); y += 16;

    // Uptime
    {
        const pt::uint64_t total_secs = ticks / 50;
        const pt::uint64_t cc         = (ticks % 50) * 2;
        const pt::uint64_t secs       = total_secs % 60;
        const pt::uint64_t mins       = (total_secs / 60) % 60;
        const pt::uint64_t hours      = total_secs / 3600;

        p = buf;
        p = w_str(p, "Uptime : ");
        p = w_pad2(p, (int)hours); *p++ = ':';
        p = w_pad2(p, (int)mins);  *p++ = ':';
        p = w_pad2(p, (int)secs);  *p++ = '.';
        p = w_pad2(p, (int)cc);
        p = w_str(p, "  (");
        p = w_dec(p, ticks);
        p = w_str(p, " ticks)");
        *p = '\0';
        panic_line(fb, y, buf); y += 16;
    }

    y += 16; // blank line

    // RIP — return address from _kernel_panic_impl = panic site in caller
    const pt::uint64_t* own_frame = reinterpret_cast<const pt::uint64_t*>(own_rbp);
    const pt::uint64_t  rip       = own_frame[1];
    {
        p = buf;
        p = w_str(p, "RIP    : ");
        p = w_hex64(p, rip);
        p = append_sym(p, rip);
        *p = '\0';
        panic_line(fb, y, buf); y += 16;
    }

    y += 16; // blank line

    // ── Register dump ────────────────────────────────────────────────────────
    struct RegPair { const char* na; pt::uint64_t va; const char* nb; pt::uint64_t vb; };
    const RegPair pairs[] = {
        {"RAX", regs.rax, "RBX", regs.rbx},
        {"RCX", regs.rcx, "RDX", regs.rdx},
        {"RSI", regs.rsi, "RDI", regs.rdi},
        {"RSP", regs.rsp, "RBP", regs.rbp},
        {" R8", regs.r8,  " R9", regs.r9 },
        {"R10", regs.r10, "R11", regs.r11},
        {"R12", regs.r12, "R13", regs.r13},
        {"R14", regs.r14, "R15", regs.r15},
    };
    for (int i = 0; i < 8; i++) {
        p = buf;
        p = w_str(p, pairs[i].na); *p++ = ':'; *p++ = ' ';
        p = w_hex64(p, pairs[i].va);
        p = w_str(p, "  ");
        p = w_str(p, pairs[i].nb); *p++ = ':'; *p++ = ' ';
        p = w_hex64(p, pairs[i].vb);
        *p = '\0';
        panic_line(fb, y, buf); y += 16;
    }

    y += 16; // blank line

    // ── Stack trace ──────────────────────────────────────────────────────────
    p = buf; p = w_str(p, "Stack trace:"); *p = '\0';
    panic_line(fb, y, buf); y += 16;

    // Frame 0: return address from _kernel_panic_impl (panic site)
    // Frame 1+: walk the saved-rbp chain from own_frame[0]
    pt::uint64_t walk_rbp = own_rbp;
    for (int frame = 0; frame < 8 && walk_rbp != 0; frame++) {
        // Basic sanity: must look like a kernel virtual address
        if (walk_rbp < 0xFFFF800000000000ULL) break;

        const pt::uint64_t* fp      = reinterpret_cast<const pt::uint64_t*>(walk_rbp);
        const pt::uint64_t  ret_rip = fp[1];

        if (ret_rip < 0xFFFF800000000000ULL) break;

        p = buf;
        p = w_str(p, "  #");
        *p++ = '0' + frame;
        p = w_str(p, "  ");
        p = w_hex64(p, ret_rip);
        p = append_sym(p, ret_rip);
        *p = '\0';
        panic_line(fb, y, buf); y += 16;

        walk_rbp = fp[0]; // advance to caller's frame
    }

    halt();
}
