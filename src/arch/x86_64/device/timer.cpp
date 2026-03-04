#include "device/timer.h"
#include "kernel.h"
#include "io.h"
#include "virtual.h"
#include "vterm.h"

extern VMM vmm;

static Timer* timer_list = nullptr;
static pt::uint64_t next_timer_id = 1;
pt::uint64_t ticks;

void init_timer(const pt::uint32_t freq)
{
	klog("[TIMER] Register for %d freq\n", freq);

	const pt::uint32_t divisor = 1193180 / freq;

	const auto low = static_cast<pt::uint8_t>(divisor & 0xFF);
	const auto high = static_cast<pt::uint8_t>(divisor >> 8 & 0xFF);


	IO::outb(ModeCommandRegister, 0x36);
	IO::outb(Channel0DataPort, low);
	IO::outb(Channel0DataPort, high);
}

pt::uint64_t get_ticks() {
	return ticks;
}

pt::uint64_t get_microseconds() {
	// Read the tick counter and latch the PIT channel-0 count atomically
	// (interrupts disabled so a timer IRQ cannot fire between the two reads).
	pt::uint64_t t;
	pt::uint16_t pit_count;

	asm volatile("cli");
	t = ticks;
	// Send latch command for channel 0 to port 0x43, then read 16-bit count.
	IO::outb(ModeCommandRegister, 0x00);
	pit_count  = (pt::uint16_t)IO::inb(Channel0DataPort);
	pit_count |= (pt::uint16_t)IO::inb(Channel0DataPort) << 8;
	asm volatile("sti");

	// PIT is in mode 3 (square wave): the count decrements by 2 per clock.
	// Divide by 2 to map into [0, DIVISOR), where DIVISOR = 1193180 / 50.
	// elapsed_counts = how many PIT clocks have passed since last tick.
	const pt::uint32_t DIVISOR      = 23863; // 1193180 / 50
	const pt::uint32_t USEC_PER_TICK = 20000; // 1000000 / 50
	pt::uint32_t elapsed = DIVISOR - (pt::uint32_t)(pit_count >> 1);
	if (elapsed > DIVISOR) elapsed = 0; // guard against wrap-around edge case

	return t * USEC_PER_TICK + (pt::uint64_t)elapsed * USEC_PER_TICK / DIVISOR;
}

pt::uint64_t timer_create(pt::uint64_t delay_ticks, bool periodic, void (*callback)(void*), void* data)
{
	Timer* new_timer = (Timer*)vmm.kcalloc(sizeof(Timer));
	if (new_timer == nullptr) {
		kernel_panic("Failed to allocate timer", NotAbleToAllocateMemory);
		return 0;
	}

	new_timer->id = next_timer_id++;
	new_timer->deadline_ticks = ticks + delay_ticks;
	new_timer->interval = delay_ticks;
	new_timer->callback = callback;
	new_timer->data = data;
	new_timer->active = true;
	new_timer->periodic = periodic;
	new_timer->next = timer_list;
	timer_list = new_timer;

	klog("[TIMER] Created timer ID %d, deadline at tick %d\n", new_timer->id, new_timer->deadline_ticks);
	return new_timer->id;
}

void timer_cancel(pt::uint64_t timer_id)
{
	Timer* current = timer_list;
	Timer* prev = nullptr;

	while (current != nullptr) {
		if (current->id == timer_id) {
			// Remove from list and free memory
			if (prev == nullptr) {
				timer_list = current->next;
			} else {
				prev->next = current->next;
			}
			vmm.kfree(current);
			vterm_printf("[TIMER] Cancelled and freed timer ID %d\n", timer_id);
			return;
		}
		prev = current;
		current = current->next;
	}
}

void check_timers()
{
	Timer* current = timer_list;
	Timer* prev = nullptr;

	while (current != nullptr) {
		if (current->active && ticks >= current->deadline_ticks) {
			if (current->callback != nullptr) {
				current->callback(current->data);
			}

			if (current->periodic) {
				current->deadline_ticks = ticks + current->interval;
				prev = current;
				current = current->next;
			} else {
				// Non-periodic timer expired - remove and free it
				Timer* to_delete = current;
				current = current->next;
				if (prev == nullptr) {
					timer_list = current;
				} else {
					prev->next = current;
				}
				vmm.kfree(to_delete);
			}
		} else {
			prev = current;
			current = current->next;
		}
	}
}

void timer_tick()
{
	ticks++;
	check_timers();
	// Scheduling is handled by irq0_schedule in idt.cpp, not here.
}

void timer_list_all()
{
	Timer* current = timer_list;
	int count = 0;

	if (current == nullptr) {
		vterm_printf("[TIMER] No active timers\n");
		return;
	}

	vterm_printf("[TIMER] Active timers:\n");
	vterm_printf("  ID  | Active | Periodic | Deadline    | Interval  | Callback\n");
	vterm_printf("------|--------|----------|-------------|-----------|----------\n");

	while (current != nullptr) {
		pt::uint64_t ticks_remaining = current->active && current->deadline_ticks > ticks ?
			current->deadline_ticks - ticks : 0;

		vterm_printf("  %d  |   %c   |    %c     | %d (%d) | %d     | %x\n",
			(int)current->id,
			current->active ? 'Y' : 'N',
			current->periodic ? 'Y' : 'N',
			(int)current->deadline_ticks,
			(int)ticks_remaining,
			(int)current->interval,
			(pt::uintptr_t)current->callback);

		count++;
		current = current->next;
	}

	vterm_printf("[TIMER] Total: %d timer(s)\n", count);
}
