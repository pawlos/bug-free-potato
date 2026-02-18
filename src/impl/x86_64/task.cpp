#include "task.h"
#include "kernel.h"
#include "virtual.h"

// Static member initialization
Task TaskScheduler::tasks[MAX_TASKS];
pt::uint32_t TaskScheduler::task_count = 0;
pt::uint32_t TaskScheduler::current_task_id = 0;
pt::uint64_t TaskScheduler::scheduler_ticks = 0;

void TaskScheduler::initialize()
{
    klog("[SCHEDULER] Initializing task scheduler (max %d tasks)\n", MAX_TASKS);

    // Clear all tasks
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].id = 0xFFFFFFFF;
        tasks[i].state = TASK_DEAD;
        tasks[i].ticks_alive = 0;
        tasks[i].kernel_stack_base = 0;
        tasks[i].kernel_stack_size = 0;
    }

    // Initialize kernel as task 0
    tasks[0].id = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].ticks_alive = 0;
    tasks[0].kernel_stack_base = 0;  // Kernel uses system stack
    tasks[0].kernel_stack_size = 0;

    task_count = 1;  // Kernel task counts as 1 task
    current_task_id = 0;
    scheduler_ticks = 0;

    klog("[SCHEDULER] Scheduler ready (kernel as task 0)\n");
}

pt::uint32_t TaskScheduler::create_task(void (*entry_fn)(), pt::size_t stack_size)
{
    if (task_count >= MAX_TASKS)
    {
        klog("[SCHEDULER] Max tasks reached\n");
        return 0xFFFFFFFF;
    }

    // Find free task slot
    Task* new_task = nullptr;
    pt::uint32_t task_id = 0;
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++)
    {
        if (tasks[i].state == TASK_DEAD)
        {
            new_task = &tasks[i];
            task_id = i;
            break;
        }
    }

    if (new_task == nullptr)
    {
        return 0xFFFFFFFF;
    }

    // Free old stack if this slot was previously used (lazy cleanup from task_exit)
    if (new_task->kernel_stack_base != 0)
    {
        vmm.kfree((void*)new_task->kernel_stack_base);
        new_task->kernel_stack_base = 0;
    }

    // Allocate kernel stack
    void* stack_mem = vmm.kmalloc(stack_size);
    if (stack_mem == nullptr)
    {
        klog("[SCHEDULER] Failed to allocate stack for task\n");
        return 0xFFFFFFFF;
    }

    // Initialize task
    new_task->id = task_id;
    new_task->state = TASK_READY;
    new_task->kernel_stack_base = (pt::uintptr_t)stack_mem;
    new_task->kernel_stack_size = stack_size;
    new_task->ticks_alive = 0;

    // Initialize context - stack pointer points to top of allocated stack
    pt::uint8_t* stack_top = (pt::uint8_t*)stack_mem + stack_size;
    new_task->context.rsp = (pt::uintptr_t)stack_top;
    new_task->context.rip = (pt::uintptr_t)entry_fn;
    new_task->context.rflags = 0x202;  // IF flag set, interrupt enabled

    // Zero out other registers
    new_task->context.rax = 0;
    new_task->context.rbx = 0;
    new_task->context.rcx = 0;
    new_task->context.rdx = 0;
    new_task->context.rsi = 0;
    new_task->context.rdi = 0;
    new_task->context.rbp = 0;
    new_task->context.r8 = 0;
    new_task->context.r9 = 0;
    new_task->context.r10 = 0;
    new_task->context.r11 = 0;
    new_task->context.r12 = 0;
    new_task->context.r13 = 0;
    new_task->context.r14 = 0;
    new_task->context.r15 = 0;

    task_count++;
    klog("[SCHEDULER] Created task %d, stack at %x, entry at %x\n", task_id, new_task->kernel_stack_base, entry_fn);

    return task_id;
}

Task* TaskScheduler::get_current_task()
{
    return &tasks[current_task_id];
}

Task* TaskScheduler::get_task(pt::uint32_t id)
{
    if (id >= MAX_TASKS) return nullptr;
    if (tasks[id].state == TASK_DEAD) return nullptr;
    return &tasks[id];
}

void TaskScheduler::scheduler_tick()
{
    scheduler_ticks++;

    // Simple round-robin scheduling
    // Find next ready task
    switch_to_next_task();
}

void TaskScheduler::switch_to_next_task()
{
    Task* current = get_current_task();
    if (current == nullptr) return;

    // Find next ready task (simple round-robin)
    pt::uint32_t next_id = current_task_id;
    pt::uint32_t attempts = 0;

    do {
        next_id = (next_id + 1) % MAX_TASKS;
        attempts++;
    } while (attempts < MAX_TASKS && (tasks[next_id].state != TASK_READY && tasks[next_id].state != TASK_RUNNING));

    if (attempts >= MAX_TASKS)
    {
        // No other task ready, stay with current
        return;
    }

    if (next_id == current_task_id)
    {
        // Already on the only task
        return;
    }

    // Safety check: ensure next task is actually valid before switching
    if (tasks[next_id].state == TASK_DEAD)
    {
        klog("[SCHEDULER] ERROR: Tried to switch to dead task %d\n", next_id);
        return;
    }

    // Switch tasks
    Task* next_task = &tasks[next_id];
#ifdef SCHEDULER_DEBUG
    klog("[SCHEDULER] Switching from task %d to task %d\n", current_task_id, next_id);
#endif

    if (current->state == TASK_RUNNING)
        current->state = TASK_READY;
    next_task->state = TASK_RUNNING;

    current_task_id = next_id;
    next_task->ticks_alive++;

    // Perform context switch (in assembly)
    task_switch(&current->context, &next_task->context);
}

void TaskScheduler::task_yield()
{
    Task* current = get_current_task();
    if (current != nullptr)
    {
        current->state = TASK_READY;
        scheduler_tick();
    }
}

void TaskScheduler::task_exit()
{
    Task* current = get_current_task();
    if (current != nullptr)
    {
        klog("[SCHEDULER] Task %d exiting\n", current->id);
        current->state = TASK_DEAD;
        task_count--;
        // Stack is intentionally NOT freed here: we're still executing on it.
        // It will be freed lazily in create_task() when this slot is reused.

        // Switch to next task; this task should never run again (state=TASK_DEAD)
        scheduler_tick();
    }
}

// Assembly implementation is in src/impl/x86_64/boot/task_switch.asm
// extern "C" void task_switch(TaskContext* old_context, TaskContext* new_context);
