#pragma once

/*
 * setjmp/longjmp for x86_64 SysV ABI.
 *
 * jmp_buf layout (8 qwords = 64 bytes):
 *   [0] rbx  [1] rbp  [2] r12  [3] r13
 *   [4] r14  [5] r15  [6] rsp  [7] rip
 */

typedef unsigned long jmp_buf[8];

/* Save the current register state.
   Returns 0 the first time; returns val (or 1 if val==0) via longjmp. */
int  setjmp (jmp_buf env);

/* Restore the state saved by setjmp and return val (or 1 if val==0)
   to the original setjmp call site. Never returns to the caller. */
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
