#include "task.h"
#include "kernel.h"
#include "virtual.h"

// Static member initialization
Task TaskScheduler::tasks[MAX_TASKS];
pt::uint32_t TaskScheduler::task_count = 0;
pt::uint32_t TaskScheduler::current_task_id = 0;
pt::uint64_t TaskScheduler::scheduler_ticks = 0;
static pt::uintptr_t kernel_cr3 = 0;  // Boot PML4 physical address

void TaskScheduler::initialize()
{
    klog("[SCHEDULER] Initializing task scheduler (max %d tasks)\n", MAX_TASKS);

    // Read the current CR3 (boot PML4 physical address) before anything else.
    asm volatile("mov %0, cr3" : "=r"(kernel_cr3));

    for (pt::uint32_t i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].id = 0xFFFFFFFF;
        tasks[i].state = TASK_DEAD;
        tasks[i].ticks_alive = 0;
        tasks[i].kernel_stack_base = 0;
        tasks[i].kernel_stack_size = 0;
        tasks[i].preempt_rsp = 0;
        tasks[i].cr3 = 0;
    }

    // Kernel is task 0; it has no allocated stack (uses the boot stack).
    // Its preempt_rsp is filled in the first time irq0 fires.
    tasks[0].id = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].cr3 = kernel_cr3;

    task_count = 1;
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
        return 0xFFFFFFFF;

    // Lazy cleanup from a previous task_exit on this slot.
    if (new_task->kernel_stack_base != 0)
    {
        vmm.kfree((void*)new_task->kernel_stack_base);
        new_task->kernel_stack_base = 0;
    }
    if (new_task->cr3 != 0 && new_task->cr3 != kernel_cr3)
    {
        vmm.free_frame(new_task->cr3);
        new_task->cr3 = 0;
    }

    void* stack_mem = vmm.kmalloc(stack_size);
    if (stack_mem == nullptr)
    {
        klog("[SCHEDULER] Failed to allocate stack for task\n");
        return 0xFFFFFFFF;
    }

    // Clone the boot PML4 for this task.  Physical frames < 4GB are accessible
    // at their identity-mapped virtual address (boot tables map phys x → virt x).
    pt::uintptr_t pml4_frame = vmm.allocate_frame();
    memcpy(reinterpret_cast<void*>(pml4_frame),
           reinterpret_cast<void*>(kernel_cr3),
           4096);

    new_task->id = task_id;
    new_task->state = TASK_READY;
    new_task->kernel_stack_base = (pt::uintptr_t)stack_mem;
    new_task->kernel_stack_size = stack_size;
    new_task->ticks_alive = 0;
    new_task->cr3 = pml4_frame;

    // Build a synthetic PUSHALL + iretq frame on the task's stack so it can
    // be started by the exact same POPALL/iretq path used for any resumed task.
    //
    // Stack layout (low addr = top of stack after all pushes):
    //   preempt_rsp → [r15][r14]...[rax]   ← PUSHALL frame (15 × 8 = 120 bytes)
    //                 [RIP][CS][RFLAGS][RSP][SS]  ← iretq frame (5 × 8 = 40 bytes)
    //
    // PUSHALL push order: rax, rbx, rcx, rdx, rbp, rsi, rdi, r8..r15
    // POPALL pop order (reverse): r15 first → rax last
    //
    pt::uint64_t* stack = (pt::uint64_t*)((pt::uint8_t*)stack_mem + stack_size);
    const pt::uint64_t stack_top = (pt::uint64_t)((pt::uint8_t*)stack_mem + stack_size);

    // iretq frame (CPU pushes in this order when servicing an interrupt):
    *(--stack) = 0x10;                  // SS  (kernel data segment)
    *(--stack) = stack_top;             // RSP (task's initial stack pointer after iretq)
    *(--stack) = 0x202;                 // RFLAGS (IF=1, reserved bit 1 set)
    *(--stack) = 0x08;                  // CS  (kernel code segment)
    *(--stack) = (pt::uint64_t)entry_fn; // RIP

    // PUSHALL frame: 15 registers, all zeroed (rax first → r15 last in push order,
    // so r15 ends up at the lowest address = preempt_rsp)
    for (int i = 0; i < 15; i++)
        *(--stack) = 0;

    new_task->preempt_rsp = (pt::uintptr_t)stack;

    task_count++;
    klog("[SCHEDULER] Created task %d, stack at %x, entry at %x\n",
         task_id, new_task->kernel_stack_base, entry_fn);

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

// Round-robin: walk the task table to find the next READY/RUNNING task.
// Saves the current RSP and switches to the next task's preempt_rsp.
// Returns the RSP to load (unchanged if no switch happened).
pt::uintptr_t TaskScheduler::do_switch_to_next(pt::uintptr_t current_rsp)
{
    pt::uint32_t next_id = current_task_id;
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++)
    {
        next_id = (next_id + 1) % MAX_TASKS;
        if (tasks[next_id].state == TASK_READY ||
            tasks[next_id].state == TASK_RUNNING)
            break;
    }

    if (next_id == current_task_id)
    {
        // No other runnable task — keep running current
        if (tasks[current_task_id].state == TASK_READY)
            tasks[current_task_id].state = TASK_RUNNING;
        return current_rsp;
    }

    if (tasks[next_id].state == TASK_DEAD)
    {
        klog("[SCHEDULER] ERROR: next task %d is dead\n", next_id);
        return current_rsp;
    }

#ifdef SCHEDULER_DEBUG
    klog("[SCHEDULER] Switching from task %d to task %d\n", current_task_id, next_id);
#endif

    if (tasks[current_task_id].state == TASK_RUNNING)
        tasks[current_task_id].state = TASK_READY;
    tasks[next_id].state = TASK_RUNNING;
    current_task_id = next_id;
    tasks[next_id].ticks_alive++;

    // Switch to the new task's address space.
    // All cloned PML4s share the same kernel (higher-half) mappings so
    // kernel code and stack remain accessible after the CR3 write.
    if (tasks[next_id].cr3 != 0)
        asm volatile("mov cr3, %0" : : "r"(tasks[next_id].cr3) : "memory");

    return tasks[next_id].preempt_rsp;
}

// Called from irq0_schedule (timer interrupt boundary).
// Saves the current task's context pointer and may preempt it.
pt::uintptr_t TaskScheduler::preempt(pt::uintptr_t rsp)
{
    scheduler_ticks++;

    // Always update preempt_rsp so it stays fresh for when this task is resumed.
    tasks[current_task_id].preempt_rsp = rsp;
    tasks[current_task_id].ticks_alive++;

    if (scheduler_ticks % SCHEDULER_QUANTUM != 0)
        return rsp;  // quantum not expired — stay with current task

    return do_switch_to_next(rsp);
}

// Called from yield_schedule (int 0x81 boundary).
// Always tries to hand off to the next ready task.
pt::uintptr_t TaskScheduler::yield_tick(pt::uintptr_t rsp)
{
    tasks[current_task_id].preempt_rsp = rsp;

    if (tasks[current_task_id].state == TASK_RUNNING)
        tasks[current_task_id].state = TASK_READY;

    return do_switch_to_next(rsp);
}

// Voluntarily yield the CPU: fire int 0x81 which goes through _int_yield_stub →
// yield_schedule → yield_tick.  The interrupt frame captures the exact return
// point so the task resumes correctly after task_yield() returns.
void TaskScheduler::task_yield()
{
    asm volatile("int 0x81");
}

void TaskScheduler::task_exit()
{
    Task* current = get_current_task();
    if (current != nullptr)
    {
        klog("[SCHEDULER] Task %d exiting\n", current->id);
        current->state = TASK_DEAD;
        task_count--;
        // Stack is freed lazily in create_task() when this slot is reused.
        // Fire yield so the scheduler switches away immediately.
        asm volatile("int 0x81");
        // Should never reach here; yield_tick won't resume a DEAD task.
        __builtin_unreachable();
    }
}
