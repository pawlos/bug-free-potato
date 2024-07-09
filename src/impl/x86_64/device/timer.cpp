#include "timer.h"
#include "kernel.h"
#include "io.h"

void init_timer(pt::uint32_t freq)
{
	klog("[TIMER] Register for %d freq\n", freq);

	const pt::uint32_t divisor = 1193180 / freq;

	const auto low = (pt::uint8_t)(divisor & 0xFF);
	const auto high = (pt::uint8_t)((divisor >> 8) & 0xFF);


	IO::outb(ModeCommandRegister, 0x36);
	IO::outb(Channel0DataPort, low);
	IO::outb(Channel0DataPort, high);
}

pt::uint64_t ticks;
void timer_routine()
{
	ticks++;
}