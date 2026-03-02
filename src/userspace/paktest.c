/* paktest.c — FAT32/VFS stress test via PAK0.PAK
 *
 * Opens PAK0.PAK and exercises the seek+read path that Quake uses.
 * Checks:
 *   1. PAK header magic
 *   2. Directory entry sanity (filepos + filelen in range)
 *   3. Forward read of first 4 bytes of every file entry
 *   4. Backward read of same entries (stress backward seek)
 *   5. Consistency: backward bytes == forward bytes
 *   6. Chunk vs byte-by-byte consistency on the first suitable entry
 *   7. File magic values for .mdl/.bsp/.wav/.spr entries
 *
 * All output goes to both the window (printf) and COM1 serial.
 * Run from the shell: exec PAKTEST.ELF
 */

#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"

/* ── PAK format ──────────────────────────────────────────────────────────── */

/* PAK directory entry (64 bytes on disk) */
typedef struct {
    char         name[56];
    unsigned int filepos;
    unsigned int filelen;
} PakEntry;

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0]
         | ((unsigned int)p[1] <<  8)
         | ((unsigned int)p[2] << 16)
         | ((unsigned int)p[3] << 24);
}

/* ── Test state ─────────────────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

static void serial_str(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    if (len) sys_write_serial(s, len);
}

static void logf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s", buf);
    serial_str(buf);
}

#define PASS(fmt, ...) do { g_pass++; logf("[PASS] " fmt "\n", ##__VA_ARGS__); } while(0)
#define FAIL(fmt, ...) do { g_fail++; logf("[FAIL] " fmt "\n", ##__VA_ARGS__); } while(0)
#define INFO(fmt, ...) do {           logf("[INFO] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ── Storage ─────────────────────────────────────────────────────────────── */

#define MAX_ENTRIES 512

static PakEntry     g_entries[MAX_ENTRIES];
static int          g_nentries = 0;
static unsigned int g_pak_size = 0;

/* First 4 bytes of each entry read during the forward pass */
static unsigned char g_fwd4[MAX_ENTRIES][4];
/* 0 = not read (filelen < 4), 1 = read ok */
static char          g_fwd_ok[MAX_ENTRIES];

/* ── Tests ───────────────────────────────────────────────────────────────── */

static int test_header(int fd)
{
    unsigned char hdr[12];
    long n = sys_read(fd, hdr, 12);
    if (n != 12) { FAIL("Header read: got %ld of 12 bytes", n); return 0; }
    if (hdr[0]!='P' || hdr[1]!='A' || hdr[2]!='C' || hdr[3]!='K') {
        FAIL("Bad magic: %02x %02x %02x %02x", hdr[0], hdr[1], hdr[2], hdr[3]);
        return 0;
    }
    unsigned int dirofs = rd32(hdr + 4);
    unsigned int dirlen = rd32(hdr + 8);
    PASS("Header OK  dirofs=%u  dirlen=%u", dirofs, dirlen);
    return 1;
}

static int read_directory(int fd)
{
    /* Seek back to beginning to re-read header for dirofs/dirlen */
    unsigned char hdr[12];
    sys_lseek(fd, 0, SEEK_SET);
    sys_read(fd, hdr, 12);
    unsigned int dirofs = rd32(hdr + 4);
    unsigned int dirlen = rd32(hdr + 8);

    /* File size */
    long fsize = sys_lseek(fd, 0, SEEK_END);
    if (fsize <= 0) { FAIL("SEEK_END failed: %ld", fsize); return 0; }
    g_pak_size = (unsigned int)fsize;
    INFO("PAK size: %u bytes", g_pak_size);

    /* Number of entries */
    g_nentries = (int)(dirlen / 64);
    if (g_nentries <= 0 || g_nentries > MAX_ENTRIES) {
        FAIL("Unreasonable entry count: %d (dirlen=%u)", g_nentries, dirlen);
        return 0;
    }

    /* Seek to directory */
    long dp = sys_lseek(fd, (long)dirofs, SEEK_SET);
    if (dp < 0 || (unsigned int)dp != dirofs) {
        FAIL("Seek to dir: got %ld want %u", dp, dirofs);
        return 0;
    }

    /* Read all entries */
    long dr = sys_read(fd, g_entries, (size_t)(g_nentries * 64));
    if (dr != (long)(g_nentries * 64)) {
        FAIL("Directory read: got %ld want %d", dr, g_nentries * 64);
        return 0;
    }
    PASS("Directory: %d entries", g_nentries);
    return 1;
}

static void test_entry_sanity(void)
{
    int bad = 0;
    int i;
    for (i = 0; i < g_nentries; i++) {
        PakEntry *e = &g_entries[i];
        /* Single file > 5 MB is suspicious for shareware Quake */
        if (e->filelen > 5u * 1024u * 1024u) {
            FAIL("Entry %3d '%-40s' filelen=%u (>5MB, suspicious)", i, e->name, e->filelen);
            bad++;
        }
        /* Out of file range */
        if (e->filepos < 12 || e->filepos + e->filelen > g_pak_size) {
            FAIL("Entry %3d '%-40s' filepos=%u filelen=%u out of PAK range (%u)",
                 i, e->name, e->filepos, e->filelen, g_pak_size);
            bad++;
        }
    }
    if (bad == 0)
        PASS("All %d entries have sane filepos+filelen", g_nentries);
}

static void test_forward(int fd)
{
    int ok = 0, fail = 0;
    int i;
    INFO("Forward pass (%d entries)...", g_nentries);
    for (i = 0; i < g_nentries; i++) {
        PakEntry *e = &g_entries[i];
        g_fwd_ok[i] = 0;
        if (e->filelen < 4) { g_skip++; continue; }

        long p = sys_lseek(fd, (long)e->filepos, SEEK_SET);
        if (p < 0 || (unsigned int)p != e->filepos) {
            FAIL("Fwd seek %d '%s': got %ld want %u", i, e->name, p, e->filepos);
            fail++;
            continue;
        }
        unsigned char buf[4];
        long r = sys_read(fd, buf, 4);
        if (r != 4) {
            FAIL("Fwd read %d '%s': got %ld", i, e->name, r);
            fail++;
            continue;
        }
        g_fwd4[i][0] = buf[0];
        g_fwd4[i][1] = buf[1];
        g_fwd4[i][2] = buf[2];
        g_fwd4[i][3] = buf[3];
        g_fwd_ok[i] = 1;
        ok++;
    }
    INFO("Forward: %d ok, %d fail", ok, fail);
    if (fail == 0)
        PASS("Forward pass complete (%d entries)", ok);
    else
        FAIL("Forward pass: %d errors", fail);
}

static void test_backward(int fd)
{
    int ok = 0, fail = 0;
    int i;
    INFO("Backward pass (%d entries)...", g_nentries);
    for (i = g_nentries - 1; i >= 0; i--) {
        PakEntry *e = &g_entries[i];
        if (!g_fwd_ok[i]) continue;

        long p = sys_lseek(fd, (long)e->filepos, SEEK_SET);
        if (p < 0 || (unsigned int)p != e->filepos) {
            FAIL("Bwd seek %d '%s': got %ld want %u", i, e->name, p, e->filepos);
            fail++;
            continue;
        }
        unsigned char buf[4];
        long r = sys_read(fd, buf, 4);
        if (r != 4) {
            FAIL("Bwd read %d '%s': got %ld", i, e->name, r);
            fail++;
            continue;
        }
        if (buf[0] != g_fwd4[i][0] || buf[1] != g_fwd4[i][1] ||
            buf[2] != g_fwd4[i][2] || buf[3] != g_fwd4[i][3]) {
            FAIL("MISMATCH %d '%s': fwd=%02x%02x%02x%02x bwd=%02x%02x%02x%02x",
                 i, e->name,
                 g_fwd4[i][0], g_fwd4[i][1], g_fwd4[i][2], g_fwd4[i][3],
                 buf[0], buf[1], buf[2], buf[3]);
            fail++;
        } else {
            ok++;
        }
    }
    INFO("Backward: %d ok, %d fail", ok, fail);
    if (fail == 0)
        PASS("Backward pass + consistency (%d entries)", ok);
    else
        FAIL("Backward pass: %d errors", fail);
}

static void test_chunk_vs_byte(int fd)
{
    int target = -1;
    int i;
    for (i = 0; i < g_nentries; i++) {
        if (g_entries[i].filelen >= 64 && g_fwd_ok[i]) { target = i; break; }
    }
    if (target < 0) { INFO("Chunk test: no suitable entry"); g_skip++; return; }

    PakEntry *e = &g_entries[target];
    INFO("Chunk test on entry %d '%s' (filelen=%u)...", target, e->name, e->filelen);

    unsigned char chunk[64], bytes[64];

    /* 64-byte chunk read */
    sys_lseek(fd, (long)e->filepos, SEEK_SET);
    long rc = sys_read(fd, chunk, 64);
    if (rc != 64) { FAIL("Chunk read got %ld", rc); return; }

    /* Byte-by-byte read of same 64 bytes */
    sys_lseek(fd, (long)e->filepos, SEEK_SET);
    int j;
    for (j = 0; j < 64; j++) {
        unsigned char b;
        long r = sys_read(fd, &b, 1);
        if (r != 1) { FAIL("Byte read at offset %d got %ld", j, r); return; }
        bytes[j] = b;
    }

    int mismatch = 0;
    for (j = 0; j < 64; j++) {
        if (chunk[j] != bytes[j]) {
            FAIL("Chunk/byte offset %d: chunk=%02x byte=%02x", j, chunk[j], bytes[j]);
            mismatch++;
        }
    }
    if (!mismatch)
        PASS("Chunk vs byte-by-byte OK for '%s'", e->name);
}

static void test_magic(void)
{
    int ok = 0, bad = 0;
    int i;
    INFO("Checking file magic values...");
    for (i = 0; i < g_nentries; i++) {
        PakEntry *e = &g_entries[i];
        if (!g_fwd_ok[i]) continue;
        int nlen = (int)strlen(e->name);
        if (nlen < 4) continue;
        const char *ext = e->name + nlen - 4;

        if (strcmp(ext, ".mdl") == 0) {
            if (g_fwd4[i][0]=='I' && g_fwd4[i][1]=='D' &&
                g_fwd4[i][2]=='P' && g_fwd4[i][3]=='O') {
                ok++;
            } else {
                FAIL("MDL '%s' bad magic %02x%02x%02x%02x", e->name,
                     g_fwd4[i][0], g_fwd4[i][1], g_fwd4[i][2], g_fwd4[i][3]);
                bad++;
            }
        } else if (strcmp(ext, ".bsp") == 0) {
            /* BSP version 29 stored as little-endian uint32 */
            unsigned int ver = rd32(g_fwd4[i]);
            if (ver == 29) {
                ok++;
            } else {
                FAIL("BSP '%s' bad version %u (want 29)", e->name, ver);
                bad++;
            }
        } else if (strcmp(ext, ".wav") == 0) {
            if (g_fwd4[i][0]=='R' && g_fwd4[i][1]=='I' &&
                g_fwd4[i][2]=='F' && g_fwd4[i][3]=='F') {
                ok++;
            } else {
                FAIL("WAV '%s' bad magic %02x%02x%02x%02x", e->name,
                     g_fwd4[i][0], g_fwd4[i][1], g_fwd4[i][2], g_fwd4[i][3]);
                bad++;
            }
        } else if (strcmp(ext, ".spr") == 0) {
            if (g_fwd4[i][0]=='I' && g_fwd4[i][1]=='D' &&
                g_fwd4[i][2]=='S' && g_fwd4[i][3]=='P') {
                ok++;
            } else {
                FAIL("SPR '%s' bad magic %02x%02x%02x%02x", e->name,
                     g_fwd4[i][0], g_fwd4[i][1], g_fwd4[i][2], g_fwd4[i][3]);
                bad++;
            }
        }
    }
    if (bad == 0)
        PASS("File magic checks: %d OK", ok);
    else
        FAIL("File magic: %d bad out of %d checked", bad, ok + bad);
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    INFO("=== PAK VFS Stress Test (potatOS) ===");

    int fd = sys_open("PAK0.PAK");
    if (fd < 0) {
        FAIL("Cannot open PAK0.PAK (errno=%d) — is PAK0.PAK on the disk?", fd);
        INFO("Results: %d PASS  %d FAIL  %d SKIP", g_pass, g_fail, g_skip);
        return 1;
    }
    PASS("Opened PAK0.PAK fd=%d", fd);

    if (!test_header(fd))               goto out;
    if (!read_directory(fd))            goto out;
    test_entry_sanity();
    test_forward(fd);
    test_backward(fd);
    test_chunk_vs_byte(fd);
    test_magic();

out:
    sys_close(fd);
    INFO("=== Results: %d PASS  %d FAIL  %d SKIP ===", g_pass, g_fail, g_skip);
    return g_fail > 0 ? 1 : 0;
}
