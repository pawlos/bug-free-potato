#include "timer.h"


void init_timer(uint32_t freq)
{
	klog("[TIMER] Register for %d freq\n", freq);

	uint32_t divisor = 1193180 / freq;

	uint8_t low = (uint8_t)(divisor & 0xFF);
	uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);


	IO::outb(ModeCommandRegister, 0x36);
	IO::outb(Channel0DataPort, low);
	IO::outb(Channel0DataPort, high);
}

uint64_t ticks;
void timer_routine()
{
	ticks++;
}