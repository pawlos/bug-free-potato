#include "task.h"
#include "kernel.h"
#include "virtual.h"
#include "tss.h"
#include "fat12.h"
#include "elf_loader.h"

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
        tasks[i].user_mode = false;
        tasks[i].user_stack_base = 0;
        tasks[i].priv_pdpt = 0;
        tasks[i].priv_pd   = 0;
        tasks[i].priv_pt   = 0;
    }

    // Kernel is task 0; it has no allocated stack (uses the boot stack).
    // Its preempt_rsp is filled in the first time irq0 fires.
    tasks[0].id = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].cr3 = kernel_cr3;
    tasks[0].user_mode = false;

    task_count = 1;
    current_task_id = 0;
    scheduler_ticks = 0;

    klog("[SCHEDULER] Scheduler ready (kernel as task 0)\n");
}

pt::uint32_t TaskScheduler::create_task(void (*entry_fn)(), pt::size_t stack_size, bool user_mode)
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
    if (new_task->user_stack_base != 0)
    {
        vmm.kfree((void*)new_task->user_stack_base);
        new_task->user_stack_base = 0;
    }
    if (new_task->cr3 != 0 && new_task->cr3 != kernel_cr3)
    {
        vmm.free_frame(new_task->cr3);
        new_task->cr3 = 0;
    }

    // Lazy cleanup of private ELF code frames from a previous create_elf_task.
    if (new_task->priv_pt != 0) {
        pt::uint64_t* pt_entries = reinterpret_cast<pt::uint64_t*>(new_task->priv_pt);
        for (int i = 0; i < 512; i++) {
            if (pt_entries[i] & 0x01)
                vmm.free_frame(pt_entries[i] & ~(pt::uintptr_t)0xFFF);
        }
        vmm.free_frame(new_task->priv_pt);
        new_task->priv_pt = 0;
    }
    if (new_task->priv_pd != 0) {
        vmm.free_frame(new_task->priv_pd);
        new_task->priv_pd = 0;
    }
    if (new_task->priv_pdpt != 0) {
        vmm.free_frame(new_task->priv_pdpt);
        new_task->priv_pdpt = 0;
    }

    // Reset per-task file descriptor table for this slot.
    for (pt::size_t i = 0; i < Task::MAX_FDS; i++)
        new_task->fd_table[i].open = false;

    void* stack_mem = vmm.kmalloc(stack_size);
    if (stack_mem == nullptr)
    {
        klog("[SCHEDULER] Failed to allocate stack for task\n");
        return 0xFFFFFFFF;
    }

    // Clone the boot PML4 for this task.  Physical frames < 4GB are accessible
    // at their identity-mapped virtual address (boot tables map phys x → virt x).
    pt::uintptr_t pml4_frame = vmm.allocate_frame();
    klog("[TASK] Per-task PML4 frame: phys=%lx (kernel_cr3=%lx)\n", pml4_frame, kernel_cr3);
    memcpy(reinterpret_cast<void*>(pml4_frame),
           reinterpret_cast<void*>(kernel_cr3),
           4096);

    // Verify the copy: PML4[0] covers VA 0x0-0x7FFFFFFF (identity map, user-accessible)
    // PML4[256] covers VA 0xFFFF800000000000+ (higher-half kernel alias)
    pt::uint64_t* pml4 = reinterpret_cast<pt::uint64_t*>(pml4_frame);
    klog("[TASK] Cloned PML4[0]=%lx PML4[256]=%lx\n", pml4[0], pml4[256]);

    new_task->id = task_id;
    new_task->state = TASK_READY;
    new_task->kernel_stack_base = (pt::uintptr_t)stack_mem;
    new_task->kernel_stack_size = stack_size;
    new_task->ticks_alive = 0;
    new_task->cr3 = pml4_frame;
    new_task->user_mode = user_mode;
    new_task->user_stack_base = 0;
    new_task->priv_pdpt = 0;
    new_task->priv_pd   = 0;
    new_task->priv_pt   = 0;

    // For ring-3 tasks, allocate a separate user execution stack.
    //
    // The kernel_stack is used exclusively as the interrupt/syscall stack
    // (TSS RSP0).  When an int fires the CPU pushes an interrupt frame below
    // RSP0, then the handler saves all registers via PUSHALL.  If user code
    // were also running with RSP inside that same region the interrupt frame
    // would silently overwrite saved return addresses and locals, causing the
    // corrupted-stack crash observed when any syscall is made from C code.
    //
    // Keeping the two stacks in separate allocations eliminates the collision:
    //   kernel_stack → used only during ring-3 → ring-0 transitions (TSS RSP0)
    //   user_stack   → used only by ring-3 C/asm code (call, ret, push, pop)
    pt::uint64_t user_rsp;
    if (user_mode) {
        void* user_stack_mem = vmm.kmalloc(USER_STACK_SIZE);
        if (user_stack_mem == nullptr)
        {
            klog("[SCHEDULER] Failed to allocate user stack for task\n");
            vmm.kfree(stack_mem);
            vmm.free_frame(pml4_frame);
            return 0xFFFFFFFF;
        }
        new_task->user_stack_base = (pt::uintptr_t)user_stack_mem;
        user_rsp = (pt::uint64_t)((pt::uint8_t*)user_stack_mem + USER_STACK_SIZE);
    } else {
        user_rsp = (pt::uint64_t)((pt::uint8_t*)stack_mem + stack_size);
    }

    // Build a synthetic PUSHALL + iretq frame on the KERNEL stack so it can
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

    // iretq frame — segments depend on target privilege level.
    // Ring-0: CS=0x08 (kernel code), SS=0x10 (kernel data)
    // Ring-3: CS=0x1B (user code | RPL3), SS=0x23 (user data | RPL3)
    if (user_mode) {
        *(--stack) = 0x23;               // SS  (user data, DPL=3)
        *(--stack) = user_rsp;           // RSP (top of separate user stack)
        *(--stack) = 0x202;              // RFLAGS (IF=1)
        *(--stack) = 0x1B;               // CS  (user code, DPL=3)
    } else {
        *(--stack) = 0x10;               // SS  (kernel data)
        *(--stack) = user_rsp;           // RSP (top of kernel stack)
        *(--stack) = 0x202;              // RFLAGS (IF=1)
        *(--stack) = 0x08;               // CS  (kernel code)
    }
    *(--stack) = (pt::uint64_t)entry_fn; // RIP

    // PUSHALL frame: 15 registers, all zeroed (rax first → r15 last in push order,
    // so r15 ends up at the lowest address = preempt_rsp)
    for (int i = 0; i < 15; i++)
        *(--stack) = 0;

    new_task->preempt_rsp = (pt::uintptr_t)stack;

    task_count++;
    klog("[SCHEDULER] Created task %d, kstack=%lx user_rsp=%lx entry=%lx\n",
         task_id, new_task->kernel_stack_base, user_rsp, (pt::uint64_t)entry_fn);

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

    // For user-mode tasks, update TSS.RSP0 to this task's kernel stack top.
    // The CPU reads RSP0 on every ring-3 → ring-0 privilege switch and uses
    // it as the initial kernel RSP before pushing the interrupt frame.
    if (tasks[next_id].user_mode && tasks[next_id].kernel_stack_base != 0)
        tss_set_rsp0(tasks[next_id].kernel_stack_base + tasks[next_id].kernel_stack_size);

    // Switch to the new task's address space.
    // All cloned PML4s share the same kernel (higher-half) mappings so
    // kernel code and stack remain accessible after the CR3 write.
    if (tasks[next_id].cr3 != 0) {
#ifdef SCHEDULER_DEBUG
        if (tasks[next_id].user_mode) {
            pt::uint64_t* pml4 = reinterpret_cast<pt::uint64_t*>(tasks[next_id].cr3);
            klog("[SCHED] Loading CR3=%lx for task %d: PML4[0]=%lx PML4[256]=%lx\n",
                 tasks[next_id].cr3, next_id, pml4[0], pml4[256]);
        }
#endif
        asm volatile("mov cr3, %0" : : "r"(tasks[next_id].cr3) : "memory");
    }

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

void TaskScheduler::kill_user_tasks()
{
    for (pt::uint32_t i = 1; i < MAX_TASKS; i++)
    {
        Task* t = &tasks[i];
        if (t->state == TASK_DEAD || !t->user_mode)
            continue;

        klog("[SCHEDULER] kill_user_tasks: killing task %d\n", i);

        // Close any open file descriptors.
        for (pt::size_t fd = 0; fd < Task::MAX_FDS; fd++) {
            if (t->fd_table[fd].open)
                FAT12::close_file(&t->fd_table[fd]);
        }

        // Free user execution stack.
        if (t->user_stack_base != 0) {
            vmm.kfree(reinterpret_cast<void*>(t->user_stack_base));
            t->user_stack_base = 0;
        }

        // Free kernel interrupt stack.
        if (t->kernel_stack_base != 0) {
            vmm.kfree(reinterpret_cast<void*>(t->kernel_stack_base));
            t->kernel_stack_base = 0;
        }

        // Free per-task PML4 frame.
        if (t->cr3 != 0 && t->cr3 != kernel_cr3) {
            vmm.free_frame(t->cr3);
            t->cr3 = 0;
        }

        // Free private ELF code frames and page table frames.
        if (t->priv_pt != 0) {
            pt::uint64_t* pt_entries = reinterpret_cast<pt::uint64_t*>(t->priv_pt);
            for (int j = 0; j < 512; j++) {
                if (pt_entries[j] & 0x01)
                    vmm.free_frame(pt_entries[j] & ~(pt::uintptr_t)0xFFF);
            }
            vmm.free_frame(t->priv_pt);
            t->priv_pt = 0;
        }
        if (t->priv_pd != 0) {
            vmm.free_frame(t->priv_pd);
            t->priv_pd = 0;
        }
        if (t->priv_pdpt != 0) {
            vmm.free_frame(t->priv_pdpt);
            t->priv_pdpt = 0;
        }

        t->state = TASK_DEAD;
        task_count--;
    }
}

// Physical base of the ELF staging area (identity-mapped).
// ElfLoader writes ELF segments here (via VA 0xFFFF800018000000).
// create_elf_task copies from here to task-private code frames before
// the task is scheduled, so a subsequent load cannot corrupt running tasks.
static constexpr pt::uintptr_t ELF_STAGING_PHYS = 0x18000000;

// PML4/PDPT/PD indices for VA 0xFFFF800018000000
static constexpr pt::size_t ELF_PML4_IDX  = 256;
static constexpr pt::size_t ELF_PDPT_IDX  = 0;
static constexpr pt::size_t ELF_PD_IDX    = 192;

pt::uint32_t TaskScheduler::create_elf_task(const char* filename)
{
    // 1. Load ELF into the shared staging area (phys 0x18000000 / VA 0xFFFF800018000000).
    pt::size_t code_size = 0;
    pt::uintptr_t entry = ElfLoader::load(filename, &code_size);
    if (entry == 0) {
        klog("[ELF_TASK] Failed to load '%s'\n", filename);
        return 0xFFFFFFFF;
    }
    if (code_size == 0) {
        klog("[ELF_TASK] No loadable segments in '%s'\n", filename);
        return 0xFFFFFFFF;
    }

    pt::size_t num_code_frames = (code_size + 4095) / 4096;
    if (num_code_frames > 512) {
        klog("[ELF_TASK] ELF too large: %d pages\n", (int)num_code_frames);
        return 0xFFFFFFFF;
    }

    // 2. Read boot PDPT and PD addresses from kernel PML4.
    pt::uint64_t* kernel_pml4  = reinterpret_cast<pt::uint64_t*>(kernel_cr3);
    pt::uintptr_t boot_pdpt_phys = kernel_pml4[ELF_PML4_IDX] & ~(pt::uintptr_t)0xFFF;
    pt::uint64_t* boot_pdpt    = reinterpret_cast<pt::uint64_t*>(boot_pdpt_phys);
    pt::uintptr_t boot_pd_phys   = boot_pdpt[ELF_PDPT_IDX]  & ~(pt::uintptr_t)0xFFF;

    // 3. Allocate private PDPT, PD, PT frames; copy boot tables so all
    //    other higher-half mappings (kernel heap, stack, etc.) are preserved.
    pt::uintptr_t priv_pdpt_frame = vmm.allocate_frame();
    pt::uintptr_t priv_pd_frame   = vmm.allocate_frame();
    pt::uintptr_t priv_pt_frame   = vmm.allocate_frame();

    memcpy(reinterpret_cast<void*>(priv_pdpt_frame),
           reinterpret_cast<void*>(boot_pdpt_phys), 4096);
    memcpy(reinterpret_cast<void*>(priv_pd_frame),
           reinterpret_cast<void*>(boot_pd_phys), 4096);

    pt::uint64_t* priv_pt = reinterpret_cast<pt::uint64_t*>(priv_pt_frame);
    for (int i = 0; i < 512; i++)
        priv_pt[i] = 0;

    // 4. Allocate private code frames; copy ELF code from staging area.
    //    The staging area lives at its identity-mapped physical address.
    for (pt::size_t i = 0; i < num_code_frames; i++) {
        pt::uintptr_t frame = vmm.allocate_frame();
        memcpy(reinterpret_cast<void*>(frame),
               reinterpret_cast<void*>(ELF_STAGING_PHYS + i * 4096),
               4096);
        priv_pt[i] = frame | 0x07;  // Present | Writable | User
    }

    // 5. Wire private page tables:
    //    priv_pd[192]  → priv_pt  (4KB pages, PS bit clear)
    //    priv_pdpt[0]  → priv_pd
    pt::uint64_t* priv_pd   = reinterpret_cast<pt::uint64_t*>(priv_pd_frame);
    pt::uint64_t* priv_pdpt = reinterpret_cast<pt::uint64_t*>(priv_pdpt_frame);
    priv_pd[ELF_PD_IDX]     = priv_pt_frame   | 0x07;  // no PS bit = 4KB pages
    priv_pdpt[ELF_PDPT_IDX] = priv_pd_frame   | 0x07;

    // 6. Create the task (allocates kernel/user stacks and clones boot PML4).
    pt::uint32_t task_id = create_task(reinterpret_cast<void(*)()>(entry),
                                       TASK_STACK_SIZE, true);
    if (task_id == 0xFFFFFFFF) {
        // Free everything on failure.
        for (int i = 0; i < 512; i++) {
            if (priv_pt[i] & 0x01)
                vmm.free_frame(priv_pt[i] & ~(pt::uintptr_t)0xFFF);
        }
        vmm.free_frame(priv_pt_frame);
        vmm.free_frame(priv_pd_frame);
        vmm.free_frame(priv_pdpt_frame);
        return 0xFFFFFFFF;
    }

    // 7. Override task PML4[256] to point to the private PDPT.
    Task* task = &tasks[task_id];
    pt::uint64_t* task_pml4 = reinterpret_cast<pt::uint64_t*>(task->cr3);
    task_pml4[ELF_PML4_IDX] = priv_pdpt_frame | 0x07;

    task->priv_pdpt = priv_pdpt_frame;
    task->priv_pd   = priv_pd_frame;
    task->priv_pt   = priv_pt_frame;

    klog("[ELF_TASK] Task %d: '%s' entry=%lx %d code pages\n",
         task_id, filename, entry, (int)num_code_frames);

    return task_id;
}

void TaskScheduler::task_exit()
{
    Task* current = get_current_task();
    if (current != nullptr)
    {
        klog("[SCHEDULER] Task %d exiting\n", current->id);
        // Close any file descriptors left open by this task.
        for (pt::size_t i = 0; i < Task::MAX_FDS; i++) {
            if (current->fd_table[i].open)
                FAT12::close_file(&current->fd_table[i]);
        }
        // Free the user execution stack eagerly (safe here; we're about to
        // switch away and will never return to user_rsp).
        if (current->user_stack_base != 0)
        {
            vmm.kfree((void*)current->user_stack_base);
            current->user_stack_base = 0;
        }
        current->state = TASK_DEAD;
        task_count--;
        // Stack is freed lazily in create_task() when this slot is reused.
        // Fire yield so the scheduler switches away immediately.
        asm volatile("int 0x81");
        // Should never reach here; yield_tick won't resume a DEAD task.
        __builtin_unreachable();
    }
}
