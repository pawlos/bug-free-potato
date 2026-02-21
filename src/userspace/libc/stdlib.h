#pragma once
#include "syscall.h"  /* for size_t */

void  *malloc(size_t size);
void   free(void *ptr);
void   exit(int code) __attribute__((noreturn));

/* Integer to string.  Writes into buf (caller must provide enough space).
   base: 10 for decimal, 16 for hex.  Returns buf. */
char  *itoa(long val, char *buf, int base);
char  *utoa(unsigned long val, char *buf, int base);
