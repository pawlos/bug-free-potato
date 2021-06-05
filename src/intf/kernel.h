#pragma once
#include "com.h"

#define ASMCALL extern "C"


void inline halt()
{
	for(;;) {
		asm("hlt");
	}
}

void inline kernel_panic(const char *str, int reason)
{
	uint64_t rax, rbx, rcx, rdx, rsi, rdi;
	uint64_t r8, r9, r10, r11, r12;
	uint64_t r13, r14, r15, rip;

	asm __volatile__("mov %0, rax" : "=r"(rax));
	asm __volatile__("mov %0, rbx" : "=r"(rbx));
	asm __volatile__("mov %0, rcx" : "=r"(rcx));
	asm __volatile__("mov %0, rdx" : "=r"(rdx));
	asm __volatile__("mov %0, rsi" : "=r"(rsi));
	asm __volatile__("mov %0, rdi" : "=r"(rdi));
	asm __volatile__("mov %0, r8"  : "=r"(r8));
	asm __volatile__("mov %0, r9"  : "=r"(r9));
	asm __volatile__("mov %0, r10" : "=r"(r10));
	asm __volatile__("mov %0, r11" : "=r"(r11));
	asm __volatile__("mov %0, r12" : "=r"(r12));
	asm __volatile__("mov %0, r13" : "=r"(r13));
	asm __volatile__("mov %0, r14" : "=r"(r14));
	asm __volatile__("mov %0, r15" : "=r"(r15));
	asm __volatile__("lea rax, [rip]; mov %0, rax" : "=r"(rip));

	ComDevice device;

	device.print_str("Kernel_panic: %s - %x\n", str, reason);
	device.print_str("RAX: %x\tRBX: %x\n", rax, rbx);
	device.print_str("RCX: %x\tRDX: %x\n", rcx, rdx);
	device.print_str("RDX: %x\n", rdx);
	device.print_str("RSI: %x\tRDI: %x\n", rsi, rdi);
	device.print_str(" R8: %x\t R9: %x\n",  r8,  r9);
	device.print_str("R10: %x\tR11: %x\n", r10, r11);
	device.print_str("R12: %x\tR13: %x\n", r12, r13);
	device.print_str("R14: %x\tR15: %x\n", r14, r15);
	device.print_str("RIP: %x\n", rip);
	
	halt();
}