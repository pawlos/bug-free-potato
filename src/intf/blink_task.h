#pragma once

// Task function: blinks a green dot and renders the current HH:MM clock
// in the top-right corner of the framebuffer. Runs as a cooperative task
// and calls TaskScheduler::task_yield() each iteration.
void blink_task_fn();
