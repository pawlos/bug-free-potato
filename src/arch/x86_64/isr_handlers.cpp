#include "kernel.h"
#include "virtual.h"
#include "task.h"

ASMCALL void isr0_handler()
{
	kernel_panic("Divide by zero", 0);
}

ASMCALL void isr1_handler()
{
	kernel_panic("Debug", 0);
}

ASMCALL void isr2_handler()
{
	kernel_panic("NMI", 2);
}

ASMCALL void isr3_handler()
{
	kernel_panic("Debug", 3);
}

ASMCALL void isr4_handler()
{
	kernel_panic("Overflow", 4);
}

ASMCALL void isr5_handler()
{
	kernel_panic("Bound range exceeded", 5);
}

ASMCALL void isr6_handler(pt::uint64_t* frame)
{
	// frame[15] = faulting RIP (+120), frame[16] = CS (+128)
	// For ring-3 faults: frame[18] = user RSP (+144), frame[19] = SS (+152)
	pt::uint64_t rip    = frame[15];
	pt::uint64_t cs     = frame[16];
	pt::uint64_t rflags = frame[17];
	pt::uint64_t rsp    = frame[18];
	pt::uint64_t ss     = frame[19];
	pt::uint64_t rbp    = frame[10];  // saved rbp: PUSHALL index 10 (+80)
	klog("[ISR6] #UD at RIP=%lx CS=%lx RFLAGS=%lx RSP=%lx SS=%lx rbp=%lx (ring-%d)\n",
	     rip, cs, rflags, rsp, ss, rbp, (int)(cs & 3));
	// Dump opcodes at the faulting address.
	const pt::uint8_t* code = reinterpret_cast<const pt::uint8_t*>(rip);
	klog("[ISR6] opcodes @ RIP: %x %x %x %x %x %x %x %x\n",
	     (unsigned)code[0], (unsigned)code[1], (unsigned)code[2], (unsigned)code[3],
	     (unsigned)code[4], (unsigned)code[5], (unsigned)code[6], (unsigned)code[7]);
	kernel_panic("Invalid opcode", 6);
}

ASMCALL void isr7_handler()
{
	kernel_panic("Device not available", 7);
}

ASMCALL void isr8_handler()
{
	kernel_panic("Double fault", 8);
}

ASMCALL void isr9_handler()
{
	kernel_panic("Coprocessor segment overrun", 9);
}

ASMCALL void isr10_handler()
{
	kernel_panic("Invalid TSS", 10);
}

ASMCALL void isr11_handler()
{
	kernel_panic("Segment not present", 11);
}

ASMCALL void isr12_handler()
{
	kernel_panic("Stack segment fault", 12);
}

ASMCALL void isr13_handler(pt::uint64_t* frame)
{
	// frame[14] = rax = error code (popped before PUSHALL, stored at +112)
	// frame[15] = faulting RIP (+120)
	// frame[16] = CS (+128)
	// frame[17] = RFLAGS (+136)
	// frame[18] = user RSP (+144) — only valid if ring change (CS & 3)
	// frame[19] = SS (+152)       — only valid if ring change
	pt::uint64_t err_code = frame[14];
	pt::uint64_t rip      = frame[15];
	pt::uint64_t cs       = frame[16];
	pt::uint64_t rflags   = frame[17];
	klog("[ISR13] #GP errcode=%lx RIP=%lx CS=%lx RFLAGS=%lx (ring-%d)\n",
	     err_code, rip, cs, rflags, (int)(cs & 3));
	// Dump opcodes at the faulting address
	const pt::uint8_t* code = reinterpret_cast<const pt::uint8_t*>(rip);
	klog("[ISR13] opcodes @ RIP: %x %x %x %x %x %x %x %x\n",
	     (unsigned)code[0], (unsigned)code[1], (unsigned)code[2], (unsigned)code[3],
	     (unsigned)code[4], (unsigned)code[5], (unsigned)code[6], (unsigned)code[7]);
	// PUSHALL order (idt.asm): rax rbx rcx rdx rbp rsi rdi r8..r15
	// After PUSHALL, frame[0]=r15 [1]=r14 [2]=r13 [3]=r12 [4]=r11 [5]=r10
	// [6]=r9 [7]=r8 [8]=rdi [9]=rsi [10]=rbp [11]=rdx [12]=rcx [13]=rbx
	// [14]=rax(=errcode for isr13) [15]=RIP [16]=CS [17]=RFLAGS [18]=RSP [19]=SS
	klog("[ISR13] rdi=%lx rsi=%lx rbp=%lx rdx=%lx rcx=%lx rbx=%lx\n",
	     frame[8], frame[9], frame[10], frame[11], frame[12], frame[13]);
	if (cs & 3) {
		pt::uint64_t rsp = frame[18];
		pt::uint64_t ss  = frame[19];
		klog("[ISR13] ring-3 RSP=%lx SS=%lx\n", rsp, ss);
	}
	kernel_panic("General protection fault", 13);
}

ASMCALL void isr14_handler(pt::uintptr_t frame)
{
	pt::uintptr_t cr2;
	asm __volatile__("mov %%cr2, %0" : "=r"(cr2));
	pt::uint64_t err_code = *reinterpret_cast<pt::uint64_t*>(frame + 112);
	pt::uint64_t user_rip = *reinterpret_cast<pt::uint64_t*>(frame + 120);
	pt::uint64_t user_rsp = *reinterpret_cast<pt::uint64_t*>(frame + 144);
	pt::uint64_t user_rbp = *reinterpret_cast<pt::uint64_t*>(frame + 80);
	pt::uint64_t user_rdi = *reinterpret_cast<pt::uint64_t*>(frame + 64);
	pt::uint64_t user_rsi = *reinterpret_cast<pt::uint64_t*>(frame + 72);
	pt::uint64_t user_rdx = *reinterpret_cast<pt::uint64_t*>(frame + 88);
	pt::uint64_t user_rcx = *reinterpret_cast<pt::uint64_t*>(frame + 96);
	pt::uint64_t user_rbx = *reinterpret_cast<pt::uint64_t*>(frame + 104);
	klog("[PAGE FAULT] cr2=%lx errcode=%lx RIP=%lx RSP=%lx\n", cr2, err_code, user_rip, user_rsp);
	klog("[PAGE FAULT] rbp=%lx rbx=%lx\n", user_rbp, user_rbx);
	pt::uint64_t user_r12 = *reinterpret_cast<pt::uint64_t*>(frame + 24);
	pt::uint64_t user_r8  = *reinterpret_cast<pt::uint64_t*>(frame + 56);
	pt::uint64_t user_r9  = *reinterpret_cast<pt::uint64_t*>(frame + 48);
	klog("[PAGE FAULT] rdi=%lx rsi=%lx rdx=%lx rcx=%lx\n", user_rdi, user_rsi, user_rdx, user_rcx);
	klog("[PAGE FAULT] r8=%lx r9=%lx r12=%lx\n", user_r8, user_r9, user_r12);

	// If the fault came from user mode (CS & 3), kill the task instead of
	// panicking the whole kernel.
	// Dump Q2 BSS state on any user-mode page fault with cr2 < 0x1000.
	pt::uint64_t user_cs = *reinterpret_cast<pt::uint64_t*>(frame + 128);
	if (user_cs & 3) {
		klog("[PAGE FAULT] User-mode fault — killing task\n");
		TaskScheduler::task_exit(139);  // 128 + SIGSEGV(11)
		return;
	}

	kernel_panic("Page fault", 14);
}

ASMCALL void isr15_handler()
{
	kernel_panic("Reserved exception 15", 15);
}

ASMCALL void isr16_handler()
{
	kernel_panic("Floating point exception", 16);
}

ASMCALL void isr17_handler()
{
	kernel_panic("Alignment check", 17);
}

ASMCALL void isr18_handler()
{
	kernel_panic("Machine check", 18);
}

ASMCALL void isr19_handler()
{
	kernel_panic("SIMD floating point exception", 19);
}

ASMCALL void isr20_handler()
{
	kernel_panic("Reserved exception 20", 20);
}

ASMCALL void isr21_handler()
{
	kernel_panic("Reserved exception 21", 21);
}

ASMCALL void isr22_handler()
{
	kernel_panic("Reserved exception 22", 22);
}

ASMCALL void isr23_handler()
{
	kernel_panic("Reserved exception 23", 23);
}

ASMCALL void isr24_handler()
{
	kernel_panic("Reserved exception 24", 24);
}

ASMCALL void isr25_handler()
{
	kernel_panic("Reserved exception 25", 25);
}

ASMCALL void isr26_handler()
{
	kernel_panic("Reserved exception 26", 26);
}

ASMCALL void isr27_handler()
{
	kernel_panic("Reserved exception 27", 27);
}

ASMCALL void isr28_handler()
{
	kernel_panic("Reserved exception 28", 28);
}

ASMCALL void isr29_handler()
{
	kernel_panic("Reserved exception 29", 29);
}

ASMCALL void isr30_handler()
{
	kernel_panic("Reserved exception 30", 30);
}

ASMCALL void isr31_handler()
{
	kernel_panic("Reserved exception 31", 31);
}
