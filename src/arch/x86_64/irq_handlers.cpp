#include "kernel.h"
#include "io.h"
#include "device/keyboard.h"
#include "device/pic.h"
#include "device/timer.h"
#include "task.h"
#include "net/net.h"

extern void mouse_routine(const pt::int8_t mouse[]);

// Called directly from the irq0 asm stub with RSP pointing to the PUSHALL frame.
// Returns the RSP to resume (same task or next task).
ASMCALL pt::uintptr_t irq0_schedule(pt::uintptr_t saved_rsp)
{
	timer_tick();
	PIC::irq_ack(0);
	return TaskScheduler::preempt(saved_rsp);
}

// Called from _int_yield_stub (int 0x81).  Cooperative yield path.
ASMCALL pt::uintptr_t yield_schedule(pt::uintptr_t saved_rsp)
{
	return TaskScheduler::yield_tick(saved_rsp);
}

ASMCALL void irq1_handler()
{
	const pt::uint8_t c = IO::inb(0x60);
	keyboard_routine(c);
	PIC::irq_ack(1);
}

static pt::uint8_t mouse_cycle = 0;
static pt::int8_t  mouse_byte[3];

ASMCALL void irq12_handler()
{
	switch(mouse_cycle)
	{
		case 0:
			mouse_byte[0] = IO::inb(0x60);
			mouse_cycle++;
			break;
		case 1:
			mouse_byte[1] = IO::inb(0x60);
			mouse_cycle++;
			break;
		case 2:
			mouse_byte[2] = IO::inb(0x60);
			mouse_cycle=0;
			mouse_routine(mouse_byte);
			break;
	}
	PIC::irq_ack(12);
}

ASMCALL void irq11_handler()
{
	RTL8139::handle_irq();
	PIC::irq_ack(11);
}

ASMCALL void irq14_handler()
{
	// IDE primary channel interrupt - just acknowledge it
	// We're using polling, so we don't need to do anything here
	PIC::irq_ack(14);
}

ASMCALL void irq15_handler()
{
	// IDE secondary channel interrupt - just acknowledge it
	// We're using polling, so we don't need to do anything here
	PIC::irq_ack(15);
}
