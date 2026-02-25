/*
 * sh.c — potatOS userland shell (ring-3)
 *
 * Runs in its own window, provides built-in commands, and launches
 * other ELF programs via fork + exec + waitpid.
 */

#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ── simple string helpers ────────────────────────────────────────────── */

static int sh_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sh_strlen(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}

static void sh_strcpy(char* dst, const char* src) {
    while ((*dst++ = *src++));
}

/* Convert string to uppercase in-place. */
static void to_upper(char* s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s -= 32;
}

/* Build "NAME.ELF" from name (uppercased); append .ELF if not present. */
static void build_elf_name(const char* name, char* out) {
    sh_strcpy(out, name);
    to_upper(out);
    int len = sh_strlen(out);
    /* Check if already ends with .ELF */
    if (len < 4 || sh_strcmp(out + len - 4, ".ELF") != 0) {
        out[len++] = '.';
        out[len++] = 'E';
        out[len++] = 'L';
        out[len++] = 'F';
        out[len]   = '\0';
    }
}

/* ── line editor ──────────────────────────────────────────────────────── */

#define LINE_MAX 127

/* Read one line from keyboard into buf (NUL-terminated, '\n' not stored).
   Uses sys_read_key polling with sys_yield() when idle. */
static void readline(char* buf) {
    int len = 0;
    buf[0] = '\0';
    for (;;) {
        long ch = sys_read_key();
        if (ch < 0) { sys_yield(); continue; }

        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            buf[len] = '\0';
            return;
        }
        if (ch == '\b' || ch == 127) {
            if (len > 0) {
                len--;
                buf[len] = '\0';
                /* erase char on terminal */
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }
        if (ch >= 0x20 && ch < 0x7F && len < LINE_MAX) {
            buf[len++] = (char)ch;
            buf[len]   = '\0';
            putchar((char)ch);
        }
    }
}

/* ── simple tokeniser ─────────────────────────────────────────────────── */

#define ARGV_MAX 16

static int tokenise(char* line, char* argv[], int max_args) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max_args - 1) {
        /* skip spaces */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        /* find end of token */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
    }
    argv[argc] = NULL;
    return argc;
}

/* ── built-in commands ────────────────────────────────────────────────── */

static void cmd_help(void) {
    puts("Built-in commands:");
    puts("  ls               list files on disk");
    puts("  cat <file>       print file contents");
    puts("  write <f> <txt>  write text to file");
    puts("  rm <file>        delete a file");
    puts("  echo [args...]   print arguments");
    puts("  clear            clear screen");
    puts("  ticks            print tick counter");
    puts("  mem              print free memory");
    puts("  disk             print disk size");
    puts("  help             show this help");
    puts("  exit             exit shell");
    puts("  <name>           exec NAME.ELF");
}

static void cmd_ls(void) {
    char name[256];
    unsigned int size = 0;
    int i = 0;
    int found = 0;
    for (;;) {
        long r = sys_readdir(i, name, &size);
        if (!r) break;
        printf("  %-24s  %u bytes\n", name, size);
        i++;
        found = 1;
    }
    if (!found) puts("(empty)");
}

static void cmd_cat(const char* filename) {
    if (!filename) { puts("usage: cat <file>"); return; }
    int fd = sys_open(filename);
    if (fd < 0) { printf("cat: cannot open '%s'\n", filename); return; }

    char buf[512];
    long n;
    while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    putchar('\n');
    sys_close(fd);
}

static void cmd_write(const char* filename, char* argv[], int argc) {
    if (!filename || argc < 3) { puts("usage: write <file> <text>"); return; }
    int fd = sys_create(filename);
    if (fd < 0) { printf("write: cannot create '%s'\n", filename); return; }
    long total = 0;
    for (int i = 2; i < argc; i++) {
        int len = sh_strlen(argv[i]);
        sys_write(fd, argv[i], (unsigned long)len);
        total += len;
        if (i < argc - 1) { sys_write(fd, " ", 1); total++; }
    }
    sys_write(fd, "\n", 1);
    total++;
    sys_close(fd);
    printf("wrote %ld bytes to %s\n", total, filename);
}

static void cmd_rm(const char* filename) {
    if (!filename) { puts("usage: rm <file>"); return; }
    long r = sys_remove(filename);
    if (r < 0) printf("rm: cannot remove '%s'\n", filename);
    else        printf("removed %s\n", filename);
}

static void cmd_echo(char* argv[], int argc) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        printf("%s", argv[i]);
    }
    putchar('\n');
}

static void cmd_clear(void) {
    putchar('\f');  /* form feed: clears client area and homes cursor */
}

static void cmd_ticks(void) {
    printf("%ld\n", sys_get_ticks());
}

static void cmd_mem(void) {
    long free_bytes = sys_mem_free();
    printf("%ld KB free\n", free_bytes / 1024);
}

static void cmd_disk(void) {
    long total = sys_disk_size();
    printf("%ld MB total\n", total / (1024 * 1024));
}

static void cmd_exec(const char* name) {
    char fname[64];
    build_elf_name(name, fname);

    long child = sys_fork();
    if (child == 0) {
        /* child */
        int r = sys_exec(fname);
        if (r < 0) printf("exec: not found: %s\n", fname);
        sys_exit(1);
    }
    /* parent */
    int code = 0;
    sys_waitpid(child, &code);
    printf("[exit %d]\n", code);
}

/* ── main shell loop ──────────────────────────────────────────────────── */

static void shell_loop(void) {
    char line[LINE_MAX + 1];
    char* argv[ARGV_MAX];

    for (;;) {
        printf("sh> ");
        readline(line);

        int argc = tokenise(line, argv, ARGV_MAX);
        if (argc == 0) continue;

        const char* cmd = argv[0];

        if (sh_strcmp(cmd, "exit") == 0) {
            break;
        } else if (sh_strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (sh_strcmp(cmd, "ls") == 0) {
            cmd_ls();
        } else if (sh_strcmp(cmd, "cat") == 0) {
            cmd_cat(argv[1]);
        } else if (sh_strcmp(cmd, "write") == 0) {
            cmd_write(argv[1], argv, argc);
        } else if (sh_strcmp(cmd, "rm") == 0) {
            cmd_rm(argv[1]);
        } else if (sh_strcmp(cmd, "echo") == 0) {
            cmd_echo(argv, argc);
        } else if (sh_strcmp(cmd, "clear") == 0) {
            cmd_clear();
        } else if (sh_strcmp(cmd, "ticks") == 0) {
            cmd_ticks();
        } else if (sh_strcmp(cmd, "mem") == 0) {
            cmd_mem();
        } else if (sh_strcmp(cmd, "disk") == 0) {
            cmd_disk();
        } else {
            cmd_exec(cmd);
        }
    }
}

int main(void) {
    long wid = sys_create_window(20, 20, 640, 400);
    (void)wid;

    puts("potatOS shell v1.0");
    puts("Type 'help' for commands.\n");

    shell_loop();

    sys_destroy_window(wid);
    return 0;
}
