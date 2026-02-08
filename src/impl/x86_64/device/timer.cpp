#include "timer.h"
#include "kernel.h"
#include "io.h"
#include "virtual.h"

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
			current->active = false;
			klog("[TIMER] Cancelled timer ID %d\n", timer_id);
			return;
		}
		prev = current;
		current = current->next;
	}
}

void check_timers()
{
	Timer* current = timer_list;

	while (current != nullptr) {
		if (current->active && ticks >= current->deadline_ticks) {
			if (current->callback != nullptr) {
				current->callback(current->data);
			}

			if (current->periodic) {
				current->deadline_ticks = ticks + current->interval;
			} else {
				current->active = false;
			}
		}
		current = current->next;
	}
}

void timer_routine()
{
	ticks++;
	check_timers();
}
