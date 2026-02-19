#pragma once
#include "com.h"

// Forward declaration — full definition in boot.h
struct boot_elf_symbols;

constexpr pt::uint8_t HolyTrinity = 255;
constexpr pt::uint8_t MemEntriesLimitReached = 254;
constexpr pt::uint8_t BootInfoNotParsed = 253;
constexpr pt::uint8_t NoSuitableRegion = 252;
constexpr pt::uint8_t NotAbleToAllocateMemory = 251;
constexpr pt::uint8_t MouseNotAcked	= 250;
constexpr pt::uint8_t NullRefNotExpected = 249;
constexpr pt::uint8_t ACPINotAvailable = 248;

#define ASMCALL extern "C"

extern ComDevice debug;
extern pt::uint64_t ticks;

struct PanicRegs {
    pt::uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
    pt::uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
};

[[noreturn]] void _kernel_panic_impl(const char* str, int reason,
                                     const PanicRegs& regs, pt::uint64_t rbp);
void panic_set_elf_symbols(const boot_elf_symbols* tag);

void inline klog(const char *str, ...)
{
#ifdef KERNEL_LOG
    va_list argptr;
    va_start(argptr, str);
    debug.print_str(str, argptr);
    va_end(argptr);
#endif
}

[[noreturn]] void inline halt()
{
    for(;;) {
        asm("hlt");
    }
}

void inline disable_interrupts()
{
    asm __volatile__("cli");
}

void inline enable_interrupts()
{
    asm __volatile__("sti");
}

[[noreturn]] inline void kernel_panic(const char *str, int reason)
{
    disable_interrupts();
    PanicRegs regs;
    pt::uint64_t rbp_val;
    asm volatile("mov %0, rax" : "=r"(regs.rax));
    asm volatile("mov %0, rbx" : "=r"(regs.rbx));
    asm volatile("mov %0, rcx" : "=r"(regs.rcx));
    asm volatile("mov %0, rdx" : "=r"(regs.rdx));
    asm volatile("mov %0, rsi" : "=r"(regs.rsi));
    asm volatile("mov %0, rdi" : "=r"(regs.rdi));
    asm volatile("mov %0, rsp" : "=r"(regs.rsp));
    asm volatile("mov %0, r8"  : "=r"(regs.r8));
    asm volatile("mov %0, r9"  : "=r"(regs.r9));
    asm volatile("mov %0, r10" : "=r"(regs.r10));
    asm volatile("mov %0, r11" : "=r"(regs.r11));
    asm volatile("mov %0, r12" : "=r"(regs.r12));
    asm volatile("mov %0, r13" : "=r"(regs.r13));
    asm volatile("mov %0, r14" : "=r"(regs.r14));
    asm volatile("mov %0, r15" : "=r"(regs.r15));
    asm volatile("mov %0, rbp" : "=r"(rbp_val));
    regs.rbp = rbp_val;
    _kernel_panic_impl(str, reason, regs, rbp_val);
}
