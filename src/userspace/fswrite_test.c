#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int passed = 0, total = 0;

static void check(const char *desc, int ok)
{
    total++;
    if (ok) { passed++; printf("  PASS  %s\n", desc); }
    else             printf("  FAIL  %s\n", desc);
}

int main(void)
{
    printf("=== FAT32 write test ===\n\n");

    /* ── 1. create + write ─────────────────────────────────────── */
    printf("1. create + write\n");
    FILE *f = fopen("WRTEST.TXT", "w");
    check("fopen(w)", f != NULL);
    if (f) {
        check("fputs line1", fputs("Hello, potatOS!\n", f) >= 0);
        check("fputs line2", fputs("Second line.\n",    f) >= 0);
        fclose(f);
    }

    /* ── 2. read back ──────────────────────────────────────────── */
    printf("\n2. read back\n");
    f = fopen("WRTEST.TXT", "r");
    check("fopen(r)", f != NULL);
    if (f) {
        char buf[64];
        check("line1 content", fgets(buf, 64, f) && strcmp(buf, "Hello, potatOS!\n") == 0);
        check("line2 content", fgets(buf, 64, f) && strcmp(buf, "Second line.\n")    == 0);
        check("EOF reached",   fgets(buf, 64, f) == NULL);
        fclose(f);
    }

    /* ── 3. truncate via fopen(w) ──────────────────────────────── */
    printf("\n3. truncate via fopen(w)\n");
    f = fopen("WRTEST.TXT", "w");
    check("fopen(w) truncate", f != NULL);
    if (f) { fputs("Overwritten!\n", f); fclose(f); }
    f = fopen("WRTEST.TXT", "r");
    if (f) {
        char buf[64];
        check("new content",  fgets(buf, 64, f) && strcmp(buf, "Overwritten!\n") == 0);
        check("file shorter", fgets(buf, 64, f) == NULL);
        fclose(f);
    }

    /* ── 4. append ─────────────────────────────────────────────── */
    printf("\n4. append\n");
    f = fopen("WRTEST.TXT", "a");
    check("fopen(a)", f != NULL);
    if (f) { fputs("Appended.\n", f); fclose(f); }
    f = fopen("WRTEST.TXT", "r");
    if (f) {
        char buf[64];
        check("orig line",     fgets(buf, 64, f) && strcmp(buf, "Overwritten!\n") == 0);
        check("appended line", fgets(buf, 64, f) && strcmp(buf, "Appended.\n")    == 0);
        fclose(f);
    }

    /* ── 5. multi-cluster binary write/read (8 KB) ─────────────── */
    printf("\n5. multi-cluster write/read (8 KB)\n");
    const int SZ = 8 * 1024;
    unsigned char *wbuf = (unsigned char *)malloc(SZ);
    unsigned char *rbuf = (unsigned char *)malloc(SZ);
    if (wbuf && rbuf) {
        for (int i = 0; i < SZ; i++) wbuf[i] = (unsigned char)(i * 7 + 3);

        f = fopen("BIGFILE.BIN", "w");
        check("create BIGFILE.BIN", f != NULL);
        if (f) {
            check("write 8 KB", (int)fwrite(wbuf, 1, SZ, f) == SZ);
            fclose(f);
        }

        f = fopen("BIGFILE.BIN", "r");
        check("re-open BIGFILE.BIN", f != NULL);
        if (f) {
            int n = (int)fread(rbuf, 1, SZ, f);
            check("read 8 KB back",  n == SZ);
            check("data integrity",  memcmp(wbuf, rbuf, SZ) == 0);
            fclose(f);
        }
    }
    free(wbuf);
    free(rbuf);

    /* ── result ────────────────────────────────────────────────── */
    printf("\n%d / %d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
