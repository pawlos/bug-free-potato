#pragma once
#include "com.h"

constexpr pt::uint8_t HolyTrinity = 255;
constexpr pt::uint8_t MemEntriesLimitReached = 254;
constexpr pt::uint8_t BootInfoNotParsed = 253;
constexpr pt::uint8_t NoSuitableRegion = 252;
constexpr pt::uint8_t NotAbleToAllocateMemory = 251;
constexpr pt::uint8_t MouseNotAcked	= 250;
constexpr pt::uint8_t NullRefNotExpected = 249;


#define ASMCALL extern "C"

static ComDevice debug;
extern pt::uint64_t ticks;

void inline klog(const char *str, ...)
{
#ifdef KERNEL_LOG
	va_list argptr;
	va_start(argptr, str);
	debug.print_str(str, argptr);
	va_end(argptr);
#endif
}

void inline halt()
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

void inline kernel_panic(const char *str, int reason)
{
	pt::uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
	pt::uint64_t r8, r9, r10, r11, r12;
	pt::uint64_t r13, r14, r15, rip;

	asm __volatile__("mov %0, rax" : "=r"(rax));
	asm __volatile__("mov %0, rbx" : "=r"(rbx));
	asm __volatile__("mov %0, rcx" : "=r"(rcx));
	asm __volatile__("mov %0, rdx" : "=r"(rdx));
	asm __volatile__("mov %0, rsi" : "=r"(rsi));
	asm __volatile__("mov %0, rdi" : "=r"(rdi));
	asm __volatile__("mov %0, rbp" : "=r"(rbp));
	asm __volatile__("mov %0, rsp" : "=r"(rsp));
	asm __volatile__("mov %0, r8"  : "=r"(r8));
	asm __volatile__("mov %0, r9"  : "=r"(r9));
	asm __volatile__("mov %0, r10" : "=r"(r10));
	asm __volatile__("mov %0, r11" : "=r"(r11));
	asm __volatile__("mov %0, r12" : "=r"(r12));
	asm __volatile__("mov %0, r13" : "=r"(r13));
	asm __volatile__("mov %0, r14" : "=r"(r14));
	asm __volatile__("mov %0, r15" : "=r"(r15));
	asm __volatile__("lea rax, [rip]; mov %0, rax" : "=r"(rip));

	klog("[PANIC] Kernel_panic: %s - %x\n", str, reason);
	klog("RAX: %x\tRBX: %x\n", rax, rbx);
	klog("RCX: %x\tRDX: %x\n", rcx, rdx);
	klog("RSI: %x\tRDI: %x\n", rsi, rdi);
	klog("RSP: %x\tRBP: %x\n", rsp, rbp);
	klog(" R8: %x\t R9: %x\n",  r8,  r9);
	klog("R10: %x\tR11: %x\n", r10, r11);
	klog("R12: %x\tR13: %x\n", r12, r13);
	klog("R14: %x\tR15: %x\n", r14, r15);
	klog("RIP: %x\n", rip);

	klog("Ticks: %d\n", ticks);

	halt();
}