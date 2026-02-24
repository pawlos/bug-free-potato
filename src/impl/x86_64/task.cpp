#include "task.h"
#include "kernel.h"
#include "virtual.h"
#include "tss.h"
#include "vfs.h"
#include "pipe.h"
#include "elf_loader.h"
#include "timer.h"
#include "window.h"

// Close a single file descriptor, dispatching on its type.
// Handles FILE (VFS), PIPE_RD and PIPE_WR (ref-count, kfree on last close).
static void close_fd(File* f)
{
    if (!f->open) return;
    if (f->type == FdType::FILE) {
        VFS::close_file(f);
    } else {
        // PIPE_RD or PIPE_WR
        PipeBuffer* pipe = pipe_get_buf(f->fs_data);
        if (f->type == FdType::PIPE_WR)
            pipe->writer_closed = true;
        pipe->ref_count--;
        if (pipe->ref_count == 0)
            vmm.kfree(pipe);
        f->open = false;
    }
}

// Static member initialization
Task TaskScheduler::tasks[MAX_TASKS];
pt::uint32_t TaskScheduler::task_count = 0;
pt::uint32_t TaskScheduler::current_task_id = 0;
pt::uint64_t TaskScheduler::scheduler_ticks = 0;
static pt::uintptr_t kernel_cr3 = 0;  // Boot PML4 physical address

// FXSAVE template captured once at scheduler init (after fninit in enable_sse).
// Copied into every new task so they all start with a clean FPU/SSE state.
static pt::uint8_t default_fxsave[512] __attribute__((aligned(16)));

// Kernel RSP captured by _syscall_stub immediately after PUSHALL.
// Declared extern in task.h and idt.asm.
pt::uintptr_t g_syscall_rsp = 0;

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
        tasks[i].priv_pdpt    = 0;
        tasks[i].priv_pd      = 0;
        tasks[i].priv_pt      = 0;
        tasks[i].parent_id        = 0xFFFFFFFF;
        tasks[i].waiting_for      = 0xFFFFFFFF;
        tasks[i].exit_code        = 0;
        tasks[i].sleep_deadline   = 0;
        tasks[i].syscall_frame_rsp = 0;
        tasks[i].window_id        = INVALID_WID;
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

    // Capture the post-fninit FPU state as the clean template for new tasks.
    asm volatile("fxsave %0" : "=m"(default_fxsave) :: "memory");

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

    new_task->sleep_deadline = 0;
    new_task->window_id      = INVALID_WID;

    // Reset per-task file descriptor table for this slot.
    for (pt::size_t i = 0; i < Task::MAX_FDS; i++)
        new_task->fd_table[i].open = false;

    // Give the new task a clean FPU/SSE state (copy of post-fninit snapshot).
    memcpy(new_task->fxsave_area, default_fxsave, 512);

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
    new_task->state = TASK_BLOCKED;  // not schedulable until preempt_rsp is set
    new_task->kernel_stack_base = (pt::uintptr_t)stack_mem;
    new_task->kernel_stack_size = stack_size;
    new_task->ticks_alive = 0;
    new_task->cr3 = pml4_frame;
    new_task->user_mode = user_mode;
    new_task->user_stack_base = 0;
    new_task->priv_pdpt    = 0;
    new_task->priv_pd      = 0;
    new_task->priv_pt      = 0;
    new_task->parent_id        = 0xFFFFFFFF;
    new_task->waiting_for      = 0xFFFFFFFF;
    new_task->exit_code        = 0;
    new_task->syscall_frame_rsp = 0;

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
    new_task->state = TASK_READY;  // frame is fully built; safe to schedule now

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
    pt::uint32_t old_id  = current_task_id;
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

    // Save outgoing task's x87/SSE state and restore the incoming task's.
    // FXSAVE/FXRSTOR require a 16-byte aligned operand (guaranteed by alignas(16)).
    asm volatile("fxsave %0"  : "=m"(tasks[old_id].fxsave_area)  :: "memory");
    asm volatile("fxrstor %0" :      : "m"(tasks[next_id].fxsave_area) : "memory");

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

    // Wake any sleeping tasks whose deadline has expired.
    // If any task was woken, force a switch immediately so it runs within
    // one tick rather than waiting up to SCHEDULER_QUANTUM ticks.
    bool woke_any = false;
    pt::uint64_t now = get_ticks();
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_BLOCKED &&
            tasks[i].sleep_deadline != 0 &&
            now >= tasks[i].sleep_deadline) {
            tasks[i].sleep_deadline = 0;
            tasks[i].state = TASK_READY;
            woke_any = true;
        }
    }

    if (!woke_any && scheduler_ticks % SCHEDULER_QUANTUM != 0)
        return rsp;  // quantum not expired, nothing woken — stay with current task

    return do_switch_to_next(rsp);
}

// Block the current task for at least ms milliseconds.
void TaskScheduler::sleep_task(pt::uint64_t ms)
{
    if (ms == 0) return;

    Task* t = &tasks[current_task_id];
    // Timer runs at 50 Hz → 20 ms per tick; round up to at least 1 tick.
    pt::uint64_t ticks_needed = ms / 20;
    if (ticks_needed == 0) ticks_needed = 1;

    t->sleep_deadline = get_ticks() + ticks_needed;
    t->state = TASK_BLOCKED;
    task_yield();  // switch away; preempt() will wake us when deadline expires
}

// Called from yield_schedule (int 0x81 boundary).
// Always tries to hand off to the next ready task.
// TASK_BLOCKED tasks are intentionally NOT set to TASK_READY here;
// they stay blocked until task_exit() of their awaited child unblocks them.
pt::uintptr_t TaskScheduler::yield_tick(pt::uintptr_t rsp)
{
    tasks[current_task_id].preempt_rsp = rsp;

    if (tasks[current_task_id].state == TASK_RUNNING)
        tasks[current_task_id].state = TASK_READY;
    // TASK_BLOCKED: leave state unchanged so do_switch_to_next skips this task.

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
        for (pt::size_t fd = 0; fd < Task::MAX_FDS; fd++)
            close_fd(&t->fd_table[fd]);

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

// Virtual address of the ELF staging area in the kernel's high half.
// ElfLoader writes ELF segments here; create_elf_task reads from here.
// Using the high-half VA (via PML4[256]) instead of the identity-mapped
// low-half (via PML4[0]) avoids any issues with the low-half mapping.
static constexpr pt::uintptr_t ELF_STAGING_PHYS = 0x18000000;
static constexpr pt::uintptr_t ELF_STAGING_VA   = KERNEL_OFFSET + ELF_STAGING_PHYS;

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
    klog("[ELF_TASK] kernel_cr3=%lx PML4[%d]=%lx boot_pdpt_phys=%lx\n",
         kernel_cr3, (int)ELF_PML4_IDX, kernel_pml4[ELF_PML4_IDX], boot_pdpt_phys);
    if (boot_pdpt_phys == 0) {
        klog("[ELF_TASK] boot_pdpt_phys is zero — PML4 not set up?\n");
        return 0xFFFFFFFF;
    }
    pt::uint64_t* boot_pdpt    = reinterpret_cast<pt::uint64_t*>(boot_pdpt_phys);
    pt::uintptr_t boot_pd_phys   = boot_pdpt[ELF_PDPT_IDX]  & ~(pt::uintptr_t)0xFFF;
    klog("[ELF_TASK] boot_pdpt[%d]=%lx boot_pd_phys=%lx\n",
         (int)ELF_PDPT_IDX, boot_pdpt[ELF_PDPT_IDX], boot_pd_phys);
    if (boot_pd_phys == 0) {
        klog("[ELF_TASK] boot_pd_phys is zero — PDPT not set up?\n");
        return 0xFFFFFFFF;
    }

    // 3. Allocate private PDPT, PD, PT frames; copy boot tables so all
    //    other higher-half mappings (kernel heap, stack, etc.) are preserved.
    pt::uintptr_t priv_pdpt_frame = vmm.allocate_frame();
    pt::uintptr_t priv_pd_frame   = vmm.allocate_frame();
    pt::uintptr_t priv_pt_frame   = vmm.allocate_frame();
    klog("[ELF_TASK] priv frames: pdpt=%lx pd=%lx pt=%lx\n",
         priv_pdpt_frame, priv_pd_frame, priv_pt_frame);

    memcpy(reinterpret_cast<void*>(priv_pdpt_frame),
           reinterpret_cast<void*>(boot_pdpt_phys), 4096);
    memcpy(reinterpret_cast<void*>(priv_pd_frame),
           reinterpret_cast<void*>(boot_pd_phys), 4096);

    pt::uint64_t* priv_pt = reinterpret_cast<pt::uint64_t*>(priv_pt_frame);
    for (int i = 0; i < 512; i++)
        priv_pt[i] = 0;

    // 4. Allocate private code frames; copy ELF code from staging area.
    //    Read from the high-half VA of the staging area (avoids low-half identity map).
    klog("[ELF_TASK] allocating %d code frames\n", (int)num_code_frames);
    for (pt::size_t i = 0; i < num_code_frames; i++) {
        if (i % 50 == 0) klog("[ELF_TASK] i=%d\n", (int)i);
        pt::uintptr_t frame = vmm.allocate_frame();
        if (!frame) { klog("[ELF_TASK] allocate_frame returned 0 at i=%d\n", (int)i); break; }
        memcpy(reinterpret_cast<void*>(frame),
               reinterpret_cast<void*>(ELF_STAGING_VA + i * 4096),
               4096);
        priv_pt[i] = frame | 0x07;  // Present | Writable | User
    }
    klog("[ELF_TASK] code frames allocated; calling create_task\n");

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

void TaskScheduler::task_exit(int exit_code)
{
    Task* current = get_current_task();
    if (current != nullptr)
    {
        klog("[SCHEDULER] Task %d exiting with code %d\n", current->id, (int)exit_code);

        // Store exit code so waitpid_task() can read it.
        current->exit_code = exit_code;

        // Close any file descriptors left open by this task.
        for (pt::size_t i = 0; i < Task::MAX_FDS; i++)
            close_fd(&current->fd_table[i]);

        // Free the user execution stack eagerly (safe here; we're about to
        // switch away and will never return to user_rsp).
        if (current->user_stack_base != 0)
        {
            vmm.kfree((void*)current->user_stack_base);
            current->user_stack_base = 0;
        }

        // Wake any parent task blocked in waitpid_task() waiting on us.
        for (pt::uint32_t i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_BLOCKED &&
                tasks[i].waiting_for == current->id) {
                tasks[i].waiting_for = 0xFFFFFFFF;
                tasks[i].state = TASK_READY;
                klog("[SCHEDULER] Unblocked parent task %d\n", i);
            }
        }

        if (current->window_id != INVALID_WID) {
            WindowManager::destroy_window(current->window_id);
            current->window_id = INVALID_WID;
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

// ─── fork_task ────────────────────────────────────────────────────────────────
//
// Clone the calling user task into a free task slot.
// The parent's kernel stack frame (PUSHALL + iretq = 160 bytes) is copied
// verbatim to the child's kernel stack and patched so that:
//   - child gets rax=0  (fork returns 0 in the child)
//   - child gets an adjusted user RSP in the same relative position inside
//     its own user stack copy
//
// Returns the child task ID to the parent.  The child will be scheduled
// normally; when it runs, POPALL+iretq resumes it at the same user RIP.
//
pt::uint32_t TaskScheduler::fork_task(pt::uintptr_t syscall_frame_rsp)
{
    if (task_count >= MAX_TASKS) {
        klog("[FORK] Max tasks reached\n");
        return (pt::uint32_t)-1;
    }

    Task* parent = get_current_task();
    if (!parent || !parent->user_mode) {
        klog("[FORK] fork from non-user task not supported\n");
        return (pt::uint32_t)-1;
    }

    // Find a free slot.
    Task* child = nullptr;
    pt::uint32_t child_id = 0xFFFFFFFF;
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_DEAD) {
            child    = &tasks[i];
            child_id = i;
            break;
        }
    }
    if (!child) {
        klog("[FORK] No free task slot\n");
        return (pt::uint32_t)-1;
    }

    // Lazy cleanup of previous occupant (mirrors create_task).
    if (child->kernel_stack_base != 0) {
        vmm.kfree((void*)child->kernel_stack_base);
        child->kernel_stack_base = 0;
    }
    if (child->user_stack_base != 0) {
        vmm.kfree((void*)child->user_stack_base);
        child->user_stack_base = 0;
    }
    if (child->cr3 != 0 && child->cr3 != kernel_cr3) {
        vmm.free_frame(child->cr3);
        child->cr3 = 0;
    }
    if (child->priv_pt != 0) {
        pt::uint64_t* old_pt = reinterpret_cast<pt::uint64_t*>(child->priv_pt);
        for (int i = 0; i < 512; i++) {
            if (old_pt[i] & 0x01)
                vmm.free_frame(old_pt[i] & ~(pt::uintptr_t)0xFFF);
        }
        vmm.free_frame(child->priv_pt);
        child->priv_pt = 0;
    }
    if (child->priv_pd != 0)   { vmm.free_frame(child->priv_pd);   child->priv_pd   = 0; }
    if (child->priv_pdpt != 0) { vmm.free_frame(child->priv_pdpt); child->priv_pdpt = 0; }

    // ── Allocate child kernel stack ──────────────────────────────────────────
    void* child_kstack_mem = vmm.kmalloc(TASK_STACK_SIZE);
    if (!child_kstack_mem) {
        klog("[FORK] Failed to allocate child kernel stack\n");
        return (pt::uint32_t)-1;
    }

    // ── Allocate + copy child user stack ────────────────────────────────────
    void* child_ustack_mem = vmm.kmalloc(USER_STACK_SIZE);
    if (!child_ustack_mem) {
        vmm.kfree(child_kstack_mem);
        klog("[FORK] Failed to allocate child user stack\n");
        return (pt::uint32_t)-1;
    }
    klog("[FORK] copying ustack: src=%lx dst=%lx size=%d\n",
         parent->user_stack_base, (pt::uintptr_t)child_ustack_mem, (int)USER_STACK_SIZE);
    memcpy(child_ustack_mem, (void*)parent->user_stack_base, USER_STACK_SIZE);

#ifdef FORK_DEBUG
    klog("[FORK_DEBUG] parent ustack=[%lx, %lx) child kstack=[%lx, %lx) child ustack=[%lx, %lx)\n",
         parent->user_stack_base, parent->user_stack_base + USER_STACK_SIZE,
         (pt::uintptr_t)child_kstack_mem, (pt::uintptr_t)child_kstack_mem + TASK_STACK_SIZE,
         (pt::uintptr_t)child_ustack_mem, (pt::uintptr_t)child_ustack_mem + USER_STACK_SIZE);
#endif

    // ── Clone PML4 ──────────────────────────────────────────────────────────

    klog("[FORK] allocating child PML4\n");
    pt::uintptr_t child_pml4_frame = vmm.allocate_frame();
    klog("[FORK] child PML4 frame=%lx, copying parent cr3=%lx\n", child_pml4_frame, parent->cr3);
    memcpy((void*)child_pml4_frame, (void*)parent->cr3, 4096);

    // ── Deep-copy private ELF code page table frames ─────────────────────
    klog("[FORK] allocating new PDPT/PD/PT frames\n");
    pt::uintptr_t new_pdpt_frame = vmm.allocate_frame();
    pt::uintptr_t new_pd_frame   = vmm.allocate_frame();
    pt::uintptr_t new_pt_frame   = vmm.allocate_frame();
    klog("[FORK] new_pdpt=%lx new_pd=%lx new_pt=%lx\n", new_pdpt_frame, new_pd_frame, new_pt_frame);
    klog("[FORK] copying parent priv_pdpt=%lx\n", parent->priv_pdpt);
    memcpy((void*)new_pdpt_frame, (void*)parent->priv_pdpt, 4096);
    klog("[FORK] copying parent priv_pd=%lx\n", parent->priv_pd);
    memcpy((void*)new_pd_frame,   (void*)parent->priv_pd,   4096);

    pt::uint64_t* parent_pt = reinterpret_cast<pt::uint64_t*>(parent->priv_pt);
    pt::uint64_t* new_pt    = reinterpret_cast<pt::uint64_t*>(new_pt_frame);
    for (int i = 0; i < 512; i++) {
        if (parent_pt[i] & 0x01) {
            pt::uintptr_t src_frame = parent_pt[i] & ~(pt::uintptr_t)0xFFF;
            pt::uintptr_t dst_frame = vmm.allocate_frame();
            memcpy((void*)dst_frame, (void*)src_frame, 4096);
            new_pt[i] = dst_frame | 0x07;
        } else {
            new_pt[i] = 0;
        }
    }

    // Wire private page tables in the child.
    pt::uint64_t* new_pd   = reinterpret_cast<pt::uint64_t*>(new_pd_frame);
    pt::uint64_t* new_pdpt = reinterpret_cast<pt::uint64_t*>(new_pdpt_frame);
    new_pd[ELF_PD_IDX]          = new_pt_frame   | 0x07;
    new_pdpt[ELF_PDPT_IDX]      = new_pd_frame   | 0x07;
    pt::uint64_t* child_pml4    = reinterpret_cast<pt::uint64_t*>(child_pml4_frame);
    child_pml4[ELF_PML4_IDX]    = new_pdpt_frame  | 0x07;

    // ── Build child kernel stack frame ───────────────────────────────────────
    // Copy the parent's 160-byte PUSHALL+iretq frame verbatim, then patch it.
    klog("[FORK] page tables wired; building child kernel stack frame\n");
    pt::uint8_t* child_kstack_top =
        (pt::uint8_t*)child_kstack_mem + TASK_STACK_SIZE - 160;
    klog("[FORK] syscall_frame_rsp=%lx child_kstack_top=%lx\n",
         syscall_frame_rsp, (pt::uintptr_t)child_kstack_top);
    memcpy(child_kstack_top, (void*)syscall_frame_rsp, 160);

    pt::uint64_t* frame = reinterpret_cast<pt::uint64_t*>(child_kstack_top);

    // frame[14] = rax slot → 0 (fork returns 0 in child)
    frame[14] = 0;

    // frame[18] = user RSP slot → adjusted into child's user stack copy.
    pt::uint64_t parent_user_rsp = *(pt::uint64_t*)(syscall_frame_rsp + 144);
    pt::uint64_t parent_ustack_base = parent->user_stack_base;
    pt::uint64_t child_ustack_base  = (pt::uint64_t)child_ustack_mem;
    pt::uint64_t child_user_rsp =
        child_ustack_base + (parent_user_rsp - parent_ustack_base);
    frame[18] = child_user_rsp;
    klog("[FORK] parent_user_rsp=%lx parent_ustack_base=%lx child_ustack_base=%lx child_user_rsp=%lx\n",
         parent_user_rsp, parent_ustack_base, child_ustack_base, child_user_rsp);

    // frame[10] = rbp slot → also needs adjustment, for the same reason as RSP.
    //
    // Without this fix the child resumes inside __scN's epilogue with the
    // PARENT's rbp value.  The first thing __scN does post-int-0x80 is:
    //   mov [rbp-0x8], rax   (store return value)
    // which would write into the PARENT's user stack, corrupting e.g. the
    // call-return address stored there and causing the parent to fault.
    //
    // We also walk the entire rbp frame-chain in the child's COPIED user stack
    // and adjust every saved-rbp slot that still points into the parent's stack
    // range, translating it to the corresponding child stack address.
    {
        const pt::uint64_t delta   = child_ustack_base - parent_ustack_base;
        const pt::uint64_t stk_bot = parent_ustack_base;
        const pt::uint64_t stk_top = parent_ustack_base + USER_STACK_SIZE;

        // Fix the PUSHALL-saved rbp register (frame[10] = offset +80).
        pt::uint64_t cur = frame[10];
        if (cur >= stk_bot && cur < stk_top)
            frame[10] = cur + delta;

        // Walk the rbp frame chain in the child's copied user stack.
        // We iterate using PARENT coordinates so the loop termination is
        // consistent even after we patch the child's memory.
        while (cur >= stk_bot && cur < stk_top) {
            // The slot in the child's user stack that mirrors parent's [cur].
            pt::uint64_t* slot = reinterpret_cast<pt::uint64_t*>(
                child_ustack_base + (cur - stk_bot));
            pt::uint64_t prev = *slot;   // value at parent's [cur] = next rbp
            if (prev >= stk_bot && prev < stk_top)
                *slot = prev + delta;    // translate to child coords
            cur = prev;                  // advance to next frame (parent coords)
        }
    }

    // ── Populate child Task struct ────────────────────────────────────────────
    child->id                = child_id;
    child->state             = TASK_BLOCKED;  // not schedulable until preempt_rsp is set
    child->kernel_stack_base = (pt::uintptr_t)child_kstack_mem;
    child->kernel_stack_size = TASK_STACK_SIZE;
    child->ticks_alive       = 0;
    child->preempt_rsp       = (pt::uintptr_t)child_kstack_top;
    child->state             = TASK_READY;    // frame fully built; safe to schedule now
    child->cr3               = child_pml4_frame;
    child->user_mode         = true;
    child->user_stack_base   = child_ustack_base;
    child->priv_pdpt         = new_pdpt_frame;
    child->priv_pd           = new_pd_frame;
    child->priv_pt           = new_pt_frame;
    child->parent_id          = parent->id;
    child->waiting_for        = 0xFFFFFFFF;
    child->exit_code          = 0;
    child->sleep_deadline     = 0;
    child->syscall_frame_rsp  = 0;
    child->window_id          = INVALID_WID;

    // Copy open file descriptors (shallow copy — positions are independent).
    for (pt::size_t i = 0; i < Task::MAX_FDS; i++)
        child->fd_table[i] = parent->fd_table[i];

    // Increment ref_count for every pipe FD inherited by the child so that
    // each end is closed independently without premature buffer freeing.
    for (pt::size_t i = 0; i < Task::MAX_FDS; i++) {
        File* f = &child->fd_table[i];
        if (f->open && (f->type == FdType::PIPE_RD || f->type == FdType::PIPE_WR)) {
            PipeBuffer* pipe = pipe_get_buf(f->fs_data);
            pipe->ref_count++;
        }
    }

    task_count++;
    klog("[FORK] Forked task %d -> child %d\n", parent->id, child_id);
#ifdef FORK_DEBUG
    klog("[FORK_DEBUG] child_ustack=%lx child_user_rsp=%lx\n",
         child_ustack_base, child_user_rsp);
#endif

    return child_id;
}

// ─── exec_task ────────────────────────────────────────────────────────────────
//
// Replace the current task's ELF image with a new file.
// Does NOT allocate a new task slot, kernel stack, or user stack.
// The existing iretq frame on the kernel stack (pointed to by syscall_frame_rsp)
// is patched in-place so that _syscall_stub's POPALL+iretq jumps to the new
// entry point with a fresh user RSP.
//
pt::uint64_t TaskScheduler::exec_task(const char* filename,
                                      pt::uintptr_t syscall_frame_rsp)
{
    // 'filename' is a pointer into the calling task's private ELF code region
    // (VA 0xFFFF800018000xxx, e.g. .rodata).  We must copy it to the kernel
    // stack BEFORE switching CR3, because under kernel_cr3 that VA maps to the
    // boot staging area, not the task's private frames.
    char fname_buf[64];
    {
        int i = 0;
        while (i < 63 && filename[i]) { fname_buf[i] = filename[i]; i++; }
        fname_buf[i] = '\0';
    }

    Task* current = get_current_task();
    klog("[EXEC] Task %d: exec '%s'\n", current->id, fname_buf);

    // 1. Load ELF into the staging area.
    //
    // Switch to boot PML4 so VA 0xFFFF800018000000 maps to the physical staging
    // area (phys 0x18000000) rather than this task's private code frames.
    // Kernel heap and code remain accessible (they're in separate PD entries).
    // We pass fname_buf (kernel stack) instead of filename (ELF rodata).
    asm volatile("mov cr3, %0" : : "r"(kernel_cr3) : "memory");

    pt::size_t code_size = 0;
    pt::uintptr_t entry = ElfLoader::load(fname_buf, &code_size);
    if (entry == 0) {
        klog("[EXEC] Failed to load '%s'\n", fname_buf);
        // Restore task CR3 before returning.
        asm volatile("mov cr3, %0" : : "r"(current->cr3) : "memory");
        return (pt::uint64_t)-1;
    }
#ifdef FORK_DEBUG
    klog("[EXEC_DEBUG] Loaded '%s': entry=%lx code_size=%u\n",
         fname_buf, entry, (unsigned)code_size);
#endif

    pt::size_t num_frames = (code_size + 4095) / 4096;
    if (num_frames > 512) {
        klog("[EXEC] ELF too large (%d pages)\n", (int)num_frames);
        asm volatile("mov cr3, %0" : : "r"(current->cr3) : "memory");
        return (pt::uint64_t)-1;
    }

    // 2. Read boot PDPT/PD to seed the new private tables.
    pt::uint64_t* kernel_pml4    = reinterpret_cast<pt::uint64_t*>(kernel_cr3);
    pt::uintptr_t boot_pdpt_phys = kernel_pml4[ELF_PML4_IDX] & ~(pt::uintptr_t)0xFFF;
    if (boot_pdpt_phys == 0) {
        klog("[EXEC] boot_pdpt_phys is zero\n");
        asm volatile("mov cr3, %0" : : "r"(current->cr3) : "memory");
        return (pt::uint64_t)-1;
    }
    pt::uint64_t* boot_pdpt      = reinterpret_cast<pt::uint64_t*>(boot_pdpt_phys);
    pt::uintptr_t boot_pd_phys   = boot_pdpt[ELF_PDPT_IDX] & ~(pt::uintptr_t)0xFFF;
    if (boot_pd_phys == 0) {
        klog("[EXEC] boot_pd_phys is zero\n");
        asm volatile("mov cr3, %0" : : "r"(current->cr3) : "memory");
        return (pt::uint64_t)-1;
    }

    // 3. Allocate new private PDPT, PD, PT; copy boot tables.
    pt::uintptr_t new_pdpt_frame = vmm.allocate_frame();
    pt::uintptr_t new_pd_frame   = vmm.allocate_frame();
    pt::uintptr_t new_pt_frame   = vmm.allocate_frame();

    memcpy((void*)new_pdpt_frame, (void*)boot_pdpt_phys, 4096);
    memcpy((void*)new_pd_frame,   (void*)boot_pd_phys,   4096);

    pt::uint64_t* new_pt = reinterpret_cast<pt::uint64_t*>(new_pt_frame);
    for (int i = 0; i < 512; i++) new_pt[i] = 0;

    // 4. Copy ELF frames from staging area into new private frames.
    for (pt::size_t i = 0; i < num_frames; i++) {
        pt::uintptr_t frame = vmm.allocate_frame();
        memcpy((void*)frame,
               (void*)(ELF_STAGING_VA + i * 4096),
               4096);
        new_pt[i] = frame | 0x07;
    }

    // 5. Wire new page tables.
    pt::uint64_t* new_pd   = reinterpret_cast<pt::uint64_t*>(new_pd_frame);
    pt::uint64_t* new_pdpt = reinterpret_cast<pt::uint64_t*>(new_pdpt_frame);
    new_pd[ELF_PD_IDX]          = new_pt_frame   | 0x07;
    new_pdpt[ELF_PDPT_IDX]      = new_pd_frame   | 0x07;

    // 6. Free old private code frames and page table frames.
    if (current->priv_pt != 0) {
        pt::uint64_t* old_pt = reinterpret_cast<pt::uint64_t*>(current->priv_pt);
        for (int i = 0; i < 512; i++) {
            if (old_pt[i] & 0x01)
                vmm.free_frame(old_pt[i] & ~(pt::uintptr_t)0xFFF);
        }
        vmm.free_frame(current->priv_pt);
    }
    if (current->priv_pd   != 0) vmm.free_frame(current->priv_pd);
    if (current->priv_pdpt != 0) vmm.free_frame(current->priv_pdpt);

    // 7. Install new private tables into current task and PML4.
    current->priv_pdpt = new_pdpt_frame;
    current->priv_pd   = new_pd_frame;
    current->priv_pt   = new_pt_frame;

    pt::uint64_t* current_pml4    = reinterpret_cast<pt::uint64_t*>(current->cr3);
    current_pml4[ELF_PML4_IDX]   = new_pdpt_frame | 0x07;

    // 8. New user RSP = top of existing user stack (stack contents discarded).
    pt::uint64_t new_user_rsp = current->user_stack_base + USER_STACK_SIZE;

    // 9. Patch the live iretq frame in-place so _syscall_stub's iretq jumps
    //    to the new ELF entry point with a fresh user RSP.
    pt::uint64_t* frame = reinterpret_cast<pt::uint64_t*>(syscall_frame_rsp);
    frame[15] = entry;        // [+120] = new RIP
    frame[18] = new_user_rsp; // [+144] = new user RSP

    // 10. Switch back to task CR3 (now wired to new private tables) and flush TLB.
    klog("[EXEC] Task %d: iretq -> %lx\n", current->id, entry);
    asm volatile("mov cr3, %0" : : "r"(current->cr3) : "memory");

    return 0;
}

// ─── waitpid_task ─────────────────────────────────────────────────────────────
//
// Block the calling task until child (child_id) transitions to TASK_DEAD.
// Uses int 0x81 (yield) from ring 0 to release the CPU; task_exit() of the
// child sets TASK_READY on the parent and wakes it back up.
//
pt::uint64_t TaskScheduler::waitpid_task(pt::uint32_t child_id,
                                         int* out_exit_code)
{
    if (child_id >= MAX_TASKS) {
        klog("[WAITPID] Invalid child_id %u\n", child_id);
        return (pt::uint64_t)-1;
    }

    Task* child  = &tasks[child_id];
    Task* parent = get_current_task();

    // Validate child belongs to this parent (skip check for task 0 kernel tasks).
    if (parent->id != 0 && child->parent_id != parent->id) {
        klog("[WAITPID] Task %d is not a child of task %d\n", child_id, parent->id);
        return (pt::uint64_t)-1;
    }

    // Fast path: child already exited.
    if (child->state == TASK_DEAD) {
        if (out_exit_code) *out_exit_code = child->exit_code;
        return 0;
    }

    // Slow path: block until child exits.
    // int 0x81 from ring 0 pushes a ring-0 iretq frame (CS=0x08) and jumps
    // through _int_yield_stub → yield_tick.  yield_tick sees TASK_BLOCKED and
    // does NOT reset us to TASK_READY — we stay blocked until task_exit()
    // of the child sets our state back to TASK_READY.
    // When the scheduler picks us up again, iretq returns right here (the
    // instruction after "int 0x81") so the loop re-checks the child state.
    while (child->state != TASK_DEAD) {
        parent->waiting_for = child_id;
        parent->state       = TASK_BLOCKED;
        asm volatile("int 0x81");
        // Resumed: either the child exited (task_exit unblocked us) or a
        // spurious wakeup.  Re-check the condition.
    }

    if (out_exit_code) *out_exit_code = child->exit_code;
    klog("[WAITPID] Task %d: child %d exited with code %d\n",
         parent->id, child_id, (int)child->exit_code);

#ifdef FORK_DEBUG
    // Diagnostic: dump the REAL iretq frame and user stack contents.
    // Useful for verifying the parent's return path is not corrupted after fork.
    pt::uintptr_t real_frame = parent->syscall_frame_rsp;
    klog("[WAITPID_DEBUG] real_frame=%lx g_syscall_rsp=%lx\n",
         real_frame, g_syscall_rsp);
    if (real_frame != 0) {
        pt::uint64_t user_rsp = *(pt::uint64_t*)(real_frame + 144);
        klog("[WAITPID_DEBUG] iretq frame: RIP=%lx CS=%lx RFLAGS=%lx RSP=%lx SS=%lx\n",
             *(pt::uint64_t*)(real_frame + 120),
             *(pt::uint64_t*)(real_frame + 128),
             *(pt::uint64_t*)(real_frame + 136),
             user_rsp,
             *(pt::uint64_t*)(real_frame + 152));
        klog("[WAITPID_DEBUG] user stack dump (pre-iretq):\n");
        for (int _d = -1; _d <= 4; _d++) {
            pt::uint64_t addr = user_rsp + (pt::uint64_t)(_d * 8);
            klog("[WAITPID_DEBUG]   [%lx] = %lx%s\n",
                 addr, *(pt::uint64_t*)addr,
                 (_d == 0 ? "  <- saved rbp" :
                  _d == 1 ? "  <- return addr" : ""));
        }
    }
#endif
    return 0;
}
