#include "task.h"
#include "kernel.h"
#include "virtual.h"
#include "tss.h"
#include "fs/vfs.h"
#include "pipe.h"
#include "elf.h"
#include "elf_loader.h"
#include "device/timer.h"
#include "device/keyboard.h"
#include "window.h"

// Mask to extract the physical frame address from a leaf PTE.
// Clears the low 12 flag bits AND the high bits (including NX bit 63),
// leaving only the 52-bit physical frame address.
static constexpr pt::uintptr_t PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

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

// Free all user-space page-table frames and their mapped physical frames.
// Covers code PTs (priv_pt[]), stack PT (stack_pt), heap PTs (user_pd entries),
// and the user_pd / user_pdpt frames themselves.
//
// Uses KERNEL_OFFSET + PA for all frame accesses so it works under both
// the kernel CR3 (identity map via PML4[0]) and a task CR3 (ring-0 uses
// the kernel half at PML4[256] which maps KERNEL_OFFSET + PA).
static void free_user_pagetables(Task* t)
{
    pt::size_t num_code_pts = t->num_priv_pts;

    // 1. Free code PT frames and their mapped code frames.
    for (pt::size_t k = 0; k < num_code_pts; k++) {
        if (!t->priv_pt[k]) continue;
        pt::uint64_t* cpt = reinterpret_cast<pt::uint64_t*>(
            KERNEL_OFFSET + t->priv_pt[k]);
        for (int j = 0; j < 512; j++) {
            if (cpt[j] & 0x01)
                vmm.free_frame(cpt[j] & PTE_ADDR_MASK);
        }
        vmm.free_frame(t->priv_pt[k]);
        t->priv_pt[k] = 0;
    }
    t->num_priv_pts = 0;

    // 2. Free stack PT frame and all stack frames it maps.
    if (t->stack_pt) {
        pt::uint64_t* spt = reinterpret_cast<pt::uint64_t*>(
            KERNEL_OFFSET + t->stack_pt);
        for (int j = 0; j < 512; j++) {
            if (spt[j] & 0x01)
                vmm.free_frame(spt[j] & PTE_ADDR_MASK);
        }
        vmm.free_frame(t->stack_pt);
        t->stack_pt = 0;
    }

    // 3. Free heap PT frames and heap frames (user_pd[heap_start..USER_STACK_PD_IDX-1]).
    if (t->user_pd) {
        pt::uint64_t* upd = reinterpret_cast<pt::uint64_t*>(
            KERNEL_OFFSET + t->user_pd);
        pt::size_t heap_start = TaskScheduler::USER_CODE_PD_IDX + num_code_pts;
        for (pt::size_t i = heap_start; i < TaskScheduler::USER_STACK_PD_IDX; i++) {
            if (!(upd[i] & 0x01)) continue;
            if (upd[i] & 0x80) continue;  // 2MB huge page (boot identity entry) — not a PT
            pt::uint64_t* hpt = reinterpret_cast<pt::uint64_t*>(
                KERNEL_OFFSET + (upd[i] & PTE_ADDR_MASK));
            for (int j = 0; j < 512; j++) {
                if (hpt[j] & 0x01)
                    vmm.free_frame(hpt[j] & PTE_ADDR_MASK);
            }
            vmm.free_frame(upd[i] & PTE_ADDR_MASK);
        }
        vmm.free_frame(t->user_pd);
        t->user_pd = 0;
    }

    // 4. Free user PDPT frame.
    if (t->user_pdpt) {
        vmm.free_frame(t->user_pdpt);
        t->user_pdpt = 0;
    }

    t->user_heap_top = 0;
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

// Pending CR3 to load in irq0/yield assembly after RSP is already on the new
// task's kernel stack (high-half VA).  Avoids crashing when the old task's
// stack is at a low VA (e.g. task 0's boot stack) that is unmapped in the new
// task's CR3.  0 = no switch pending.
pt::uintptr_t g_next_cr3 = 0;

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
        tasks[i].user_pdpt    = 0;
        tasks[i].user_pd      = 0;
        for (pt::size_t j = 0; j < Task::MAX_PRIV_PTS; j++) tasks[i].priv_pt[j] = 0;
        tasks[i].num_priv_pts = 0;
        tasks[i].stack_pt     = 0;
        tasks[i].user_heap_top = 0;
        tasks[i].parent_id        = 0xFFFFFFFF;
        tasks[i].waiting_for      = 0xFFFFFFFF;
        tasks[i].exit_code        = 0;
        tasks[i].sleep_deadline   = 0;
        tasks[i].syscall_frame_rsp = 0;
        tasks[i].window_id        = INVALID_WID;
        tasks[i].owns_window      = false;
        tasks[i].vterm_id         = INVALID_VT;
    }

    // Kernel is task 0; it has no allocated stack (uses the boot stack).
    // Its preempt_rsp is filled in the first time irq0 fires.
    tasks[0].id = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].cr3 = kernel_cr3;
    tasks[0].user_mode = false;
    tasks[0].priority = 0;  // kernel shell = highest priority
    tasks[0].remaining_ticks = SCHEDULER_QUANTUM;

    task_count = 1;
    current_task_id = 0;
    scheduler_ticks = 0;

    // Reset x87 and SSE to known-good defaults right before capturing the
    // template.  fninit only touches x87 (FCW=0x037F, round-to-nearest,
    // 80-bit precision, all exceptions masked).  MXCSR must be set
    // separately for SSE (0x1F80 = all exceptions masked, round-to-nearest,
    // no DAZ/FTZ).  Doing this HERE instead of relying on the boot-time
    // fninit guarantees the template is pristine even if kernel init code
    // between boot and scheduler init accidentally touched FPU state.
    asm volatile("fninit" ::: "memory");
    {
        pt::uint32_t default_mxcsr = 0x1F80;
        asm volatile("ldmxcsr %0" : : "m"(default_mxcsr) : "memory");
    }
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

    // Lazy cleanup of user page tables from a previous create_elf_task.
    free_user_pagetables(new_task);

    new_task->sleep_deadline = 0;
    new_task->window_id      = INVALID_WID;
    new_task->owns_window    = false;
    new_task->vterm_id       = INVALID_VT;
    new_task->name[0]        = '\0';
    new_task->priority       = 1;  // normal priority by default
    new_task->remaining_ticks = SCHEDULER_QUANTUM;

    // Allocate per-task file descriptor table on the heap.
    new_task->fd_table = static_cast<File*>(vmm.kcalloc(Task::MAX_FDS * sizeof(File)));

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
    new_task->user_pdpt    = 0;
    new_task->user_pd      = 0;
    for (pt::size_t j = 0; j < Task::MAX_PRIV_PTS; j++) new_task->priv_pt[j] = 0;
    new_task->num_priv_pts = 0;
    new_task->stack_pt     = 0;
    new_task->user_heap_top = 0;
    new_task->parent_id        = 0xFFFFFFFF;
    new_task->waiting_for      = 0xFFFFFFFF;
    new_task->exit_code        = 0;
    new_task->syscall_frame_rsp = 0;

    // For ring-3 tasks, the user execution stack is mapped at USER_STACK_BOT..USER_STACK_TOP
    // by create_elf_task() after we return.  Set initial RSP to USER_STACK_TOP - 16 so
    // that:
    //   1. [rsp] = 0 (argc=0) — within mapped stack, not at the PDPT[1] boundary.
    //   2. RSP % 16 == 0 at _start — satisfies the SysV ABI requirement that RSP is
    //      16-byte aligned at the CALL instruction, so that inside main() after
    //      "push rbp; mov rbp, rsp", rbp is 16-byte aligned and SSE locals
    //      at [rbp-k*16] are also 16-byte aligned (avoids MOVAPS #GP faults).
    // The zeroed stack frames ensure [rsp] = 0 (argc=0) and [rsp+8] = 0 (argv=NULL).
    // For kernel tasks, RSP = top of the kernel stack allocation.
    pt::uint64_t user_rsp;
    if (user_mode) {
        user_rsp = USER_STACK_TOP - 16;
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

const char* TaskScheduler::get_task_name(pt::uint32_t id)
{
    if (id >= MAX_TASKS) return nullptr;
    if (tasks[id].state == TASK_DEAD) return nullptr;
    return tasks[id].name;
}

pt::uint32_t TaskScheduler::list_tasks(TaskListEntry* buf, pt::uint32_t max_entries)
{
    if (!buf || max_entries == 0) return 0;
    pt::uint32_t count = 0;
    for (pt::uint32_t i = 0; i < MAX_TASKS && count < max_entries; i++) {
        const Task& t = tasks[i];
        if (t.state == TASK_DEAD) continue;
        buf[count].id       = t.id;
        buf[count].state    = (pt::uint8_t)t.state;
        buf[count].priority = t.priority;
        buf[count].ticks    = t.ticks_alive;
        int j = 0;
        for (; j < 15 && t.name[j]; j++)
            buf[count].name[j] = t.name[j];
        for (; j < 16; j++)
            buf[count].name[j] = '\0';
        count++;
    }
    return count;
}

// Round-robin: walk the task table to find the next READY/RUNNING task.
// Saves the current RSP and switches to the next task's preempt_rsp.
// Returns the RSP to load (unchanged if no switch happened).
pt::uintptr_t TaskScheduler::do_switch_to_next(pt::uintptr_t current_rsp)
{
    pt::uint32_t old_id  = current_task_id;

    // Pass 1: find the best (lowest) priority among runnable tasks OTHER
    // than the current one.  The whole point of switching is to give
    // someone else a turn; including ourselves would let a high-priority
    // task that just yielded immediately win back the CPU.
    pt::uint8_t best_prio = 255;
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++) {
        if (i == current_task_id) continue;
        if ((tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING) &&
            tasks[i].priority < best_prio)
            best_prio = tasks[i].priority;
    }

    if (best_prio == 255) {
        // No other runnable task at all — keep running current.
        if (tasks[current_task_id].state == TASK_READY)
            tasks[current_task_id].state = TASK_RUNNING;
        return current_rsp;
    }

    // Pass 2: round-robin among runnable tasks at that priority level.
    // Each priority level has its own cursor so that a high-priority task
    // (e.g. the shell) bouncing in and out doesn't reset the round-robin
    // position for lower-priority tasks.
    static constexpr pt::size_t NUM_PRIOS = 3;
    static pt::uint32_t rr_next[NUM_PRIOS] = {0, 0, 0};
    pt::uint8_t prio_idx = best_prio < NUM_PRIOS ? best_prio : NUM_PRIOS - 1;
    pt::uint32_t next_id = 0;
    bool found = false;
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++) {
        pt::uint32_t candidate = (rr_next[prio_idx] + i) % MAX_TASKS;
        if (candidate == current_task_id) continue;
        if ((tasks[candidate].state == TASK_READY ||
             tasks[candidate].state == TASK_RUNNING) &&
            tasks[candidate].priority == best_prio) {
            next_id = candidate;
            found = true;
            break;
        }
    }

    if (!found) {
        // Pass 1 guaranteed something exists, but be safe.
        if (tasks[current_task_id].state == TASK_READY)
            tasks[current_task_id].state = TASK_RUNNING;
        return current_rsp;
    }

    // Advance the cursor past the task we just picked.
    rr_next[prio_idx] = (next_id + 1) % MAX_TASKS;

#ifdef SCHEDULER_DEBUG
    klog("[SCHEDULER] Switching from task %d to task %d\n", current_task_id, next_id);
#endif

    if (tasks[current_task_id].state == TASK_RUNNING)
        tasks[current_task_id].state = TASK_READY;
    tasks[next_id].state = TASK_RUNNING;
    current_task_id = next_id;
    tasks[next_id].ticks_alive++;
    tasks[next_id].remaining_ticks = SCHEDULER_QUANTUM;

    // For user-mode tasks, update TSS.RSP0 to this task's kernel stack top.
    // The CPU reads RSP0 on every ring-3 → ring-0 privilege switch and uses
    // it as the initial kernel RSP before pushing the interrupt frame.
    if (tasks[next_id].user_mode && tasks[next_id].kernel_stack_base != 0)
        tss_set_rsp0(tasks[next_id].kernel_stack_base + tasks[next_id].kernel_stack_size);

    // Store the new CR3 in g_next_cr3 for the assembly stub to load AFTER it
    // has already switched RSP to this task's high-half kernel stack.
    // Loading CR3 here (in C) would leave the old task's low-VA boot stack
    // active with the new task's CR3 — the 'ret' out of this function would
    // try to read a return address from an unmapped low VA and triple-fault.
    g_next_cr3 = tasks[next_id].cr3;

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

    // Time-slice preemption: switch when quantum expired or a woken task
    // needs the CPU.
    if (woke_any || --tasks[current_task_id].remaining_ticks <= 0) {
        tasks[current_task_id].remaining_ticks = SCHEDULER_QUANTUM;
        return do_switch_to_next(rsp);
    }

    return rsp;
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

        // Close any open file descriptors and free the table.
        if (t->fd_table) {
            for (pt::size_t fd = 0; fd < Task::MAX_FDS; fd++)
                close_fd(&t->fd_table[fd]);
            vmm.kfree(t->fd_table);
            t->fd_table = nullptr;
        }

        // Destroy window if task owns one.
        if (t->window_id != INVALID_WID && t->owns_window) {
            WindowManager::destroy_window(t->window_id);
        }
        t->window_id = INVALID_WID;
        t->owns_window = false;

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

        // Free all user page tables and mapped frames.
        free_user_pagetables(t);

        t->state = TASK_DEAD;
        task_count--;
    }
}

bool TaskScheduler::kill_task(pt::uint32_t pid)
{
    if (pid == 0 || pid >= MAX_TASKS)
        return false;

    Task* t = &tasks[pid];
    if (t->state == TASK_DEAD || !t->user_mode)
        return false;

    klog("[SCHEDULER] kill_task: killing task %d\n", pid);

    // Close any open file descriptors and free the table.
    if (t->fd_table) {
        for (pt::size_t fd = 0; fd < Task::MAX_FDS; fd++)
            close_fd(&t->fd_table[fd]);
        vmm.kfree(t->fd_table);
        t->fd_table = nullptr;
    }

    // Destroy window if task owns one.
    if (t->window_id != INVALID_WID && t->owns_window) {
        WindowManager::destroy_window(t->window_id);
    }
    t->window_id = INVALID_WID;
    t->owns_window = false;

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

    // Free all user page tables and mapped frames.
    free_user_pagetables(t);

    // Wake any parent blocked in waitpid.
    for (pt::uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_BLOCKED &&
            tasks[i].waiting_for == pid) {
            tasks[i].waiting_for = 0xFFFFFFFF;
            tasks[i].state = TASK_READY;
        }
    }

    t->exit_code = -1;
    t->state = TASK_DEAD;
    task_count--;
    return true;
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
// Maximum ELF pages: MAX_PRIV_PTS PT frames × 512 pages each = 16 MB
static constexpr pt::size_t MAX_ELF_PAGES = Task::MAX_PRIV_PTS * 512;

pt::uint32_t TaskScheduler::create_elf_task(const char* filename,
                                            int argc,
                                            const char* const* argv)
{
    // 1. Load ELF into the shared staging area (phys 0x18000000 / ELF_STAGING_VA).
    //    elf_loader.cpp redirects writes to ELF_STAGING_VA + (p_vaddr - USER_CODE_BASE).
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
    if (num_code_frames > MAX_ELF_PAGES) {
        klog("[ELF_TASK] ELF too large: %d pages (max %d)\n",
             (int)num_code_frames, (int)MAX_ELF_PAGES);
        return 0xFFFFFFFF;
    }
    pt::size_t num_pts = (num_code_frames + 511) / 512;
    if (num_pts == 0) num_pts = 1;

    // 2. Allocate fresh user_pdpt and user_pd frames.
    //    Populate them with the boot identity-map entries (U/S cleared → kernel-only)
    //    so ring-0 interrupt handlers can access device MMIO (framebuffer at 0xfd000000,
    //    etc.) while this task's CR3 is active.  Code/stack entries are overridden below.
    pt::uintptr_t user_pdpt_frame = vmm.allocate_frame();
    pt::uintptr_t user_pd_frame   = vmm.allocate_frame();
    pt::uint64_t* user_pdpt = reinterpret_cast<pt::uint64_t*>(user_pdpt_frame);
    pt::uint64_t* user_pd   = reinterpret_cast<pt::uint64_t*>(user_pd_frame);

    // Read boot PDPT and its PD[0] via direct PA (identity-mapped under boot CR3).
    pt::uint64_t* boot_pml4  = reinterpret_cast<pt::uint64_t*>(kernel_cr3);
    pt::uintptr_t boot_pdpt_pa = boot_pml4[0] & ~(pt::uintptr_t)0xFFF;
    pt::uint64_t* boot_pdpt  = reinterpret_cast<pt::uint64_t*>(boot_pdpt_pa);
    pt::uintptr_t boot_pd_pa   = boot_pdpt[0] & ~(pt::uintptr_t)0xFFF;
    pt::uint64_t* boot_pd    = reinterpret_cast<pt::uint64_t*>(boot_pd_pa);

    // user_pdpt[0] is wired to user_pd (with U/S=1) below.
    // user_pdpt[1..3] copy boot PDPT entries (1..4 GB identity), U/S cleared.
    memset(user_pdpt, 0, 4096);
    for (int i = 1; i < 4; i++)
        user_pdpt[i] = boot_pdpt[i] & ~(pt::uint64_t)0x04;

    // user_pd copies all 512 boot PD0 entries (0..1 GB identity, U/S cleared).
    // Code and stack entries are overridden with private PTs (U/S=1) below.
    for (int i = 0; i < 512; i++)
        user_pd[i] = boot_pd[i] & ~(pt::uint64_t)0x04;

    klog("[ELF_TASK] user_pdpt=%lx user_pd=%lx num_pts=%d\n",
         user_pdpt_frame, user_pd_frame, (int)num_pts);

    // 3. Allocate code PT frames (zeroed) and populate with code data from staging.
    pt::uintptr_t code_pt_frames[Task::MAX_PRIV_PTS];
    for (pt::size_t k = 0; k < num_pts; k++) {
        code_pt_frames[k] = vmm.allocate_frame();
        memset(reinterpret_cast<void*>(code_pt_frames[k]), 0, 4096);
    }

    klog("[ELF_TASK] allocating %d code frames across %d PT(s)\n",
         (int)num_code_frames, (int)num_pts);
    for (pt::size_t i = 0; i < num_code_frames; i++) {
        if (i % 50 == 0) klog("[ELF_TASK] i=%d\n", (int)i);
        pt::uintptr_t frame = vmm.allocate_frame();
        if (!frame) { klog("[ELF_TASK] allocate_frame returned 0 at i=%d\n", (int)i); break; }
        memcpy(reinterpret_cast<void*>(frame),
               reinterpret_cast<void*>(ELF_STAGING_VA + i * 4096),
               4096);
        // Build PTE flags from ELF segment permissions.
        // Base: Present(0x01) | User(0x04) = 0x05.
        // Add Writable(0x02) if PF_W set.
        // Add NX (bit 63) if PF_X not set.
        pt::uint8_t pflags = ElfLoader::page_flags[i];
        pt::uint64_t pte = frame | 0x05;  // Present + User
        if (pflags & PF_W) pte |= 0x02;   // Writable
        if (!(pflags & PF_X)) pte |= PTE_NX;  // No-Execute
        pt::uint64_t* cpt = reinterpret_cast<pt::uint64_t*>(code_pt_frames[i / 512]);
        cpt[i % 512] = pte;
    }

    // Wire code PTs into user_pd at USER_CODE_PD_IDX.
    for (pt::size_t k = 0; k < num_pts; k++)
        user_pd[USER_CODE_PD_IDX + k] = code_pt_frames[k] | 0x07;

    // 4. Allocate stack PT and stack frames; wire into user_pd[USER_STACK_PD_IDX].
    pt::uintptr_t stack_pt_frame = vmm.allocate_frame();
    pt::uint64_t* stack_pt = reinterpret_cast<pt::uint64_t*>(stack_pt_frame);
    memset(stack_pt, 0, 4096);

    for (pt::size_t i = 0; i < USER_STACK_PAGES; i++) {
        pt::uintptr_t frame = vmm.allocate_frame();
        memset(reinterpret_cast<void*>(frame), 0, 4096);
        stack_pt[i] = frame | 0x07 | PTE_NX;  // RW + User + No-Execute
    }
    user_pd[USER_STACK_PD_IDX] = stack_pt_frame | 0x07;

    // 5. Wire user_pdpt[0] → user_pd.
    user_pdpt[USER_PDPT_IDX] = user_pd_frame | 0x07;

    // 6. Create the task (allocates kernel stack, clones boot PML4).
    //    The task is set TASK_READY by create_task, but its PML4[0] still
    //    points to the boot PDPT.  Immediately block it so the scheduler
    //    cannot run it before we wire user_pdpt into PML4[0] below.
    klog("[ELF_TASK] code frames allocated; calling create_task\n");
    pt::uint32_t task_id = create_task(reinterpret_cast<void(*)()>(entry),
                                       TASK_STACK_SIZE, true);
    if (task_id == 0xFFFFFFFF) {
        // Free all allocated frames on failure.
        for (pt::size_t k = 0; k < num_pts; k++) {
            if (!code_pt_frames[k]) continue;
            pt::uint64_t* cpt = reinterpret_cast<pt::uint64_t*>(code_pt_frames[k]);
            for (int i = 0; i < 512; i++) {
                if (cpt[i] & 0x01) vmm.free_frame(cpt[i] & PTE_ADDR_MASK);
            }
            vmm.free_frame(code_pt_frames[k]);
        }
        for (int i = 0; i < 512; i++) {
            if (stack_pt[i] & 0x01) vmm.free_frame(stack_pt[i] & PTE_ADDR_MASK);
        }
        vmm.free_frame(stack_pt_frame);
        vmm.free_frame(user_pd_frame);
        vmm.free_frame(user_pdpt_frame);
        return 0xFFFFFFFF;
    }

    // 7. Wire new user address space into the task's PML4.
    //    Block the task while we modify its PML4 to prevent the scheduler
    //    from running it with the boot identity mapping at PML4[0].
    Task* task = &tasks[task_id];
    task->state = TASK_BLOCKED;
    pt::uint64_t* task_pml4 = reinterpret_cast<pt::uint64_t*>(task->cr3);
    // PML4[0] → user_pdpt (P|W|U): user code, heap, stack.
    task_pml4[USER_PML4_IDX] = user_pdpt_frame | 0x07;
    // PML4[256] → boot PDPT (P|W, no U bit): kernel only.
    task_pml4[ELF_PML4_IDX] &= ~(pt::uint64_t)0x04;

    // 8. Store page-table info in task struct.
    task->user_pdpt    = user_pdpt_frame;
    task->user_pd      = user_pd_frame;
    task->stack_pt     = stack_pt_frame;
    task->user_heap_top = USER_HEAP_BASE;
    task->num_priv_pts = num_pts;
    for (pt::size_t k = 0; k < num_pts; k++) task->priv_pt[k] = code_pt_frames[k];
    for (pt::size_t k = num_pts; k < Task::MAX_PRIV_PTS; k++) task->priv_pt[k] = 0;

    // Store basename for display (strip directory path, truncate to fit).
    {
        const char* base = filename;
        for (const char* p = filename; *p; p++)
            if (*p == '/') base = p + 1;
        pt::size_t i = 0;
        for (; i < sizeof(task->name) - 1 && base[i]; i++)
            task->name[i] = base[i];
        task->name[i] = '\0';
    }

    // Initialize default environment variables for the new task.
    {
        const char defaults[] = "HOME=/\0PATH=/";
        memcpy(task->env_buf, defaults, sizeof(defaults));
        task->env_buf[sizeof(defaults)] = '\0';  // double-NUL terminator
        task->env_buf_len = sizeof(defaults) + 1;
    }

    // Build initial user stack with envp so crt0 can extract environ.
    // Stack layout (high→low): [env strings][align][NULL envp][envp ptrs][NULL argv][argc=0] ← RSP
    // stack_pt entries are direct-PA accessible (identity-mapped under kernel CR3).
    {
        auto elf_write_byte = [&](pt::uint64_t va, pt::uint8_t b) {
            pt::size_t pt_idx = (va - USER_STACK_BOT) >> 12;
            pt::uintptr_t pa = (stack_pt[pt_idx] & PTE_ADDR_MASK) + (va & 0xFFF);
            *reinterpret_cast<pt::uint8_t*>(pa) = b;
        };
        auto elf_write_qword = [&](pt::uint64_t va, pt::uint64_t val) {
            pt::size_t pt_idx = (va - USER_STACK_BOT) >> 12;
            pt::uintptr_t pa = (stack_pt[pt_idx] & PTE_ADDR_MASK) + (va & 0xFFF);
            *reinterpret_cast<pt::uint64_t*>(pa) = val;
        };

        const int ENV_MAX = 16;
        const char* env_strs[ENV_MAX];
        int envc = 0;
        {
            const char* p = task->env_buf;
            const char* end = task->env_buf + task->env_buf_len;
            while (p < end && *p && envc < ENV_MAX) {
                env_strs[envc++] = p;
                while (p < end && *p) p++;
                p++;
            }
        }

        const int ARGV_MAX_LOCAL = 16;
        int real_argc = 0;
        if (argv && argc > 0)
            real_argc = (argc > ARGV_MAX_LOCAL) ? ARGV_MAX_LOCAL : argc;

        pt::uint64_t rsp = USER_STACK_TOP;

        // Write env string data downward.
        pt::uint64_t uenv_ptrs[ENV_MAX];
        for (int i = envc - 1; i >= 0; i--) {
            const char* s = env_strs[i];
            int len = 0; while (s[len]) len++;
            len++;
            rsp -= (pt::uint64_t)len;
            for (int j = 0; j < len; j++)
                elf_write_byte(rsp + (pt::uint64_t)j, (pt::uint8_t)s[j]);
            uenv_ptrs[i] = rsp;
        }

        // Write argv string data downward.
        pt::uint64_t uarg_ptrs[ARGV_MAX_LOCAL];
        for (int i = real_argc - 1; i >= 0; i--) {
            const char* s = argv[i];
            int len = 0; while (s[len]) len++;
            len++;
            rsp -= (pt::uint64_t)len;
            for (int j = 0; j < len; j++)
                elf_write_byte(rsp + (pt::uint64_t)j, (pt::uint8_t)s[j]);
            uarg_ptrs[i] = rsp;
        }

        rsp &= ~(pt::uint64_t)7;

        // Total qwords: 1(argc) + real_argc + 1(argv NULL) + envc + 1(envp NULL)
        {
            pt::uint64_t total_qw = (pt::uint64_t)(real_argc + envc + 3);
            pt::uint64_t final_rsp = rsp - total_qw * 8;
            if (final_rsp & 0xF) rsp -= 8;
        }

        // envp NULL sentinel
        rsp -= 8; elf_write_qword(rsp, 0);
        // envp pointers
        for (int i = envc - 1; i >= 0; i--) {
            rsp -= 8; elf_write_qword(rsp, uenv_ptrs[i]);
        }
        // argv NULL sentinel
        rsp -= 8; elf_write_qword(rsp, 0);
        // argv pointers
        for (int i = real_argc - 1; i >= 0; i--) {
            rsp -= 8; elf_write_qword(rsp, uarg_ptrs[i]);
        }
        // argc
        rsp -= 8; elf_write_qword(rsp, (pt::uint64_t)real_argc);

        // Patch the iretq frame's RSP to point to the new stack layout.
        pt::uint64_t* frame = reinterpret_cast<pt::uint64_t*>(task->preempt_rsp);
        frame[18] = rsp;  // user RSP in iretq frame
    }

    klog("[ELF_TASK] Task %d: '%s' entry=%lx %d code pages\n",
         task_id, filename, entry, (int)num_code_frames);

    // All page tables and stack are fully wired; safe to schedule now.
    task->state = TASK_READY;

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
        if (current->fd_table) {
            for (pt::size_t i = 0; i < Task::MAX_FDS; i++)
                close_fd(&current->fd_table[i]);
            vmm.kfree(current->fd_table);
            current->fd_table = nullptr;
        }

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

        if (current->window_id != INVALID_WID && current->owns_window) {
            WindowManager::destroy_window(current->window_id);
        }
        current->window_id = INVALID_WID;
        current->owns_window = false;

        // Zero page table pointers so lazy cleanup in create_task()
        // won't dereference stale values.  The actual frames leak, but
        // it's safe.  (We can't call free_user_pagetables here because
        // this task's CR3 is still active.)
        for (pt::size_t k = 0; k < Task::MAX_PRIV_PTS; k++)
            current->priv_pt[k] = 0;
        current->num_priv_pts = 0;
        current->stack_pt     = 0;
        current->user_pd      = 0;
        current->user_pdpt    = 0;
        current->user_heap_top = 0;

        current->state = TASK_DEAD;
        task_count--;
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
    // free_user_pagetables uses KERNEL_OFFSET-mapped accesses inside, but the
    // child slot may have been a previous task with old-style kmalloc stack
    // (user_stack_base freed above) and zero user_pd/user_pdpt/stack_pt, so
    // this is safe as a cleanup step.
    free_user_pagetables(child);

    // ── Allocate child kernel stack ──────────────────────────────────────────
    void* child_kstack_mem = vmm.kmalloc(TASK_STACK_SIZE);
    if (!child_kstack_mem) {
        klog("[FORK] Failed to allocate child kernel stack\n");
        return (pt::uint32_t)-1;
    }

    // ── Clone parent PML4 (under task CR3; use KERNEL_OFFSET + PA for frames) ──
    // All physical frame accesses below use KERNEL_OFFSET + PA because the
    // parent's PML4[0] now only maps user VAs, not the full identity range.
    klog("[FORK] allocating child PML4\n");
    pt::uintptr_t child_pml4_frame = vmm.allocate_frame();
    klog("[FORK] child PML4 frame=%lx, copying parent cr3=%lx\n",
         child_pml4_frame, parent->cr3);
    memcpy(reinterpret_cast<void*>(KERNEL_OFFSET + child_pml4_frame),
           reinterpret_cast<void*>(KERNEL_OFFSET + parent->cr3), 4096);

    // ── Allocate child user_pdpt, user_pd (fresh frames) ─────────────────────
    pt::size_t par_num_pts = parent->num_priv_pts;
    pt::uintptr_t child_user_pdpt_frame = vmm.allocate_frame();
    pt::uintptr_t child_user_pd_frame   = vmm.allocate_frame();
    pt::uint64_t* child_updpt = reinterpret_cast<pt::uint64_t*>(
        KERNEL_OFFSET + child_user_pdpt_frame);
    pt::uint64_t* child_upd = reinterpret_cast<pt::uint64_t*>(
        KERNEL_OFFSET + child_user_pd_frame);

    // Copy parent's user_pdpt/user_pd so child inherits boot identity-map entries
    // (U/S=0, kernel-only) at indices 1..3 of pdpt and most of pd.
    // Code/stack/heap entries are overwritten with deep copies below.
    memcpy(child_updpt,
           reinterpret_cast<void*>(KERNEL_OFFSET + parent->user_pdpt), 4096);
    memcpy(child_upd,
           reinterpret_cast<void*>(KERNEL_OFFSET + parent->user_pd), 4096);

    // ── Deep-copy code PT frames ───────────────────────────────────────────
    pt::uintptr_t new_pt_frames[Task::MAX_PRIV_PTS];
    for (pt::size_t k = 0; k < Task::MAX_PRIV_PTS; k++) new_pt_frames[k] = 0;

    klog("[FORK] deep-copying %d code PTs\n", (int)par_num_pts);
    for (pt::size_t k = 0; k < par_num_pts; k++) {
        new_pt_frames[k] = vmm.allocate_frame();
        pt::uint64_t* parent_pt = reinterpret_cast<pt::uint64_t*>(
            KERNEL_OFFSET + parent->priv_pt[k]);
        pt::uint64_t* new_pt = reinterpret_cast<pt::uint64_t*>(
            KERNEL_OFFSET + new_pt_frames[k]);
        for (int i = 0; i < 512; i++) {
            if (parent_pt[i] & 0x01) {
                pt::uintptr_t src_frame = parent_pt[i] & PTE_ADDR_MASK;
                pt::uint64_t  pte_flags = parent_pt[i] & 0x8000000000000FFFULL; // preserve NX + low flags
                pt::uintptr_t dst_frame = vmm.allocate_frame();
                memcpy(reinterpret_cast<void*>(KERNEL_OFFSET + dst_frame),
                       reinterpret_cast<void*>(KERNEL_OFFSET + src_frame), 4096);
                new_pt[i] = dst_frame | pte_flags;
            } else {
                new_pt[i] = 0;
            }
        }
        child_upd[USER_CODE_PD_IDX + k] = new_pt_frames[k] | 0x07;
    }

    // ── Deep-copy stack PT frames ──────────────────────────────────────────
    pt::uintptr_t child_stack_pt_frame = vmm.allocate_frame();
    pt::uint64_t* child_spt = reinterpret_cast<pt::uint64_t*>(
        KERNEL_OFFSET + child_stack_pt_frame);
    pt::uint64_t* parent_spt = reinterpret_cast<pt::uint64_t*>(
        KERNEL_OFFSET + parent->stack_pt);
    for (int i = 0; i < 512; i++) {
        if (parent_spt[i] & 0x01) {
            pt::uintptr_t src = parent_spt[i] & PTE_ADDR_MASK;
            pt::uint64_t  pte_flags = parent_spt[i] & 0x8000000000000FFFULL;
            pt::uintptr_t dst = vmm.allocate_frame();
            memcpy(reinterpret_cast<void*>(KERNEL_OFFSET + dst),
                   reinterpret_cast<void*>(KERNEL_OFFSET + src), 4096);
            child_spt[i] = dst | pte_flags;
        } else {
            child_spt[i] = 0;
        }
    }
    child_upd[USER_STACK_PD_IDX] = child_stack_pt_frame | 0x07;

    // ── Deep-copy heap PT frames ───────────────────────────────────────────
    if (parent->user_pd) {
        pt::uint64_t* parent_upd = reinterpret_cast<pt::uint64_t*>(
            KERNEL_OFFSET + parent->user_pd);
        pt::size_t heap_start = USER_CODE_PD_IDX + par_num_pts;
        for (pt::size_t i = heap_start; i < USER_STACK_PD_IDX; i++) {
            if (!(parent_upd[i] & 0x01)) continue;
            if (parent_upd[i] & 0x80) continue;  // boot 2MB huge page, already in child_upd
            pt::uintptr_t child_hpt_frame = vmm.allocate_frame();
            pt::uint64_t* par_hpt = reinterpret_cast<pt::uint64_t*>(
                KERNEL_OFFSET + (parent_upd[i] & PTE_ADDR_MASK));
            pt::uint64_t* child_hpt = reinterpret_cast<pt::uint64_t*>(
                KERNEL_OFFSET + child_hpt_frame);
            for (int j = 0; j < 512; j++) {
                if (par_hpt[j] & 0x01) {
                    pt::uintptr_t src = par_hpt[j] & PTE_ADDR_MASK;
                    pt::uint64_t  pte_flags = par_hpt[j] & 0x8000000000000FFFULL;
                    pt::uintptr_t dst = vmm.allocate_frame();
                    memcpy(reinterpret_cast<void*>(KERNEL_OFFSET + dst),
                           reinterpret_cast<void*>(KERNEL_OFFSET + src), 4096);
                    child_hpt[j] = dst | pte_flags;
                } else {
                    child_hpt[j] = 0;
                }
            }
            child_upd[i] = child_hpt_frame | 0x07;
        }
    }

    // ── Wire child user address space ──────────────────────────────────────
    child_updpt[USER_PDPT_IDX] = child_user_pd_frame | 0x07;

    pt::uint64_t* child_pml4 = reinterpret_cast<pt::uint64_t*>(
        KERNEL_OFFSET + child_pml4_frame);
    child_pml4[USER_PML4_IDX] = child_user_pdpt_frame | 0x07;
    child_pml4[ELF_PML4_IDX] &= ~(pt::uint64_t)0x04;  // clear U bit on kernel half

    // ── Build child kernel stack frame ───────────────────────────────────────
    // Copy the parent's 160-byte PUSHALL+iretq frame verbatim, then patch rax.
    // RSP and rbp need no adjustment: parent and child share the same user VA
    // layout (USER_STACK_BOT..USER_STACK_TOP), so the user RSP is identical.
    klog("[FORK] page tables wired; building child kernel stack frame\n");
    pt::uint8_t* child_kstack_top =
        (pt::uint8_t*)child_kstack_mem + TASK_STACK_SIZE - 160;
    klog("[FORK] syscall_frame_rsp=%lx child_kstack_top=%lx\n",
         syscall_frame_rsp, (pt::uintptr_t)child_kstack_top);
    memcpy(child_kstack_top, (void*)syscall_frame_rsp, 160);

    pt::uint64_t* frame = reinterpret_cast<pt::uint64_t*>(child_kstack_top);
    // frame[14] = rax slot → 0 (fork returns 0 in child)
    frame[14] = 0;
    // frame[18] = user RSP — unchanged (same user VA as parent)

    // ── Populate child Task struct ────────────────────────────────────────────
    child->id                = child_id;
    child->state             = TASK_BLOCKED;
    child->kernel_stack_base = (pt::uintptr_t)child_kstack_mem;
    child->kernel_stack_size = TASK_STACK_SIZE;
    child->ticks_alive       = 0;
    child->preempt_rsp       = (pt::uintptr_t)child_kstack_top;
    child->state             = TASK_READY;
    child->cr3               = child_pml4_frame;
    child->user_mode         = true;
    child->user_stack_base   = 0;
    child->user_pdpt         = child_user_pdpt_frame;
    child->user_pd           = child_user_pd_frame;
    child->stack_pt          = child_stack_pt_frame;
    child->user_heap_top     = parent->user_heap_top;
    child->num_priv_pts      = par_num_pts;
    for (pt::size_t k = 0; k < par_num_pts; k++) child->priv_pt[k] = new_pt_frames[k];
    for (pt::size_t k = par_num_pts; k < Task::MAX_PRIV_PTS; k++) child->priv_pt[k] = 0;
    child->parent_id          = parent->id;
    child->waiting_for        = 0xFFFFFFFF;
    child->exit_code          = 0;
    child->sleep_deadline     = 0;
    child->syscall_frame_rsp  = 0;
    child->window_id          = INVALID_WID;
    child->owns_window        = false;
    child->vterm_id           = parent->vterm_id;
    memcpy(child->name, parent->name, sizeof(child->name));

    // Inherit parent's FPU/SSE state so the child resumes with valid x87/SSE context.
    memcpy(child->fxsave_area, parent->fxsave_area, 512);

    // Allocate and copy open file descriptors (shallow copy — positions are independent).
    child->fd_table = static_cast<File*>(vmm.kcalloc(Task::MAX_FDS * sizeof(File)));
    if (parent->fd_table) {
        for (pt::size_t i = 0; i < Task::MAX_FDS; i++)
            child->fd_table[i] = parent->fd_table[i];
    }

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
                                      pt::uintptr_t syscall_frame_rsp,
                                      int argc,
                                      const char* const* argv)
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

    // Copy argv strings to kernel stack before CR3 switch.
    const int ARGV_MAX_LOCAL = 16, ARG_MAX_LOCAL = 128;
    char arg_flat[ARGV_MAX_LOCAL * ARG_MAX_LOCAL];
    const char* arg_kptrs[ARGV_MAX_LOCAL];
    int real_argc = 0;
    if (argv && argc > 0) {
        real_argc = (argc > ARGV_MAX_LOCAL) ? ARGV_MAX_LOCAL : argc;
        for (int i = 0; i < real_argc; i++) {
            char* dst = arg_flat + i * ARG_MAX_LOCAL;
            const char* src = argv[i];
            int j = 0;
            while (j < ARG_MAX_LOCAL - 1 && src[j]) { dst[j] = src[j]; j++; }
            dst[j] = '\0';
            arg_kptrs[i] = dst;
        }
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
    if (num_frames > MAX_ELF_PAGES) {
        klog("[EXEC] ELF too large (%d pages, max %d)\n", (int)num_frames, (int)MAX_ELF_PAGES);
        asm volatile("mov cr3, %0" : : "r"(current->cr3) : "memory");
        return (pt::uint64_t)-1;
    }
    pt::size_t num_pts = (num_frames + 511) / 512;
    if (num_pts == 0) num_pts = 1;

    // 2. Allocate fresh user_pdpt, user_pd, code PT frames, and stack PT.
    //    (Under kernel_cr3: identity map active, direct PA access OK.)
    pt::uintptr_t new_user_pdpt_frame = vmm.allocate_frame();
    pt::uintptr_t new_user_pd_frame   = vmm.allocate_frame();
    pt::uint64_t* new_user_pdpt = reinterpret_cast<pt::uint64_t*>(new_user_pdpt_frame);
    pt::uint64_t* new_user_pd   = reinterpret_cast<pt::uint64_t*>(new_user_pd_frame);

    // Populate with boot identity-map (U/S cleared → kernel-only) so ring-0
    // interrupt handlers can access device MMIO under this task's CR3.
    pt::uint64_t* boot_pml4x  = reinterpret_cast<pt::uint64_t*>(kernel_cr3);
    pt::uintptr_t boot_pdpt_pax = boot_pml4x[0] & ~(pt::uintptr_t)0xFFF;
    pt::uint64_t* boot_pdptx  = reinterpret_cast<pt::uint64_t*>(boot_pdpt_pax);
    pt::uintptr_t boot_pd_pax  = boot_pdptx[0] & ~(pt::uintptr_t)0xFFF;
    pt::uint64_t* boot_pdx    = reinterpret_cast<pt::uint64_t*>(boot_pd_pax);

    memset(new_user_pdpt, 0, 4096);
    for (int i = 1; i < 4; i++)
        new_user_pdpt[i] = boot_pdptx[i] & ~(pt::uint64_t)0x04;
    for (int i = 0; i < 512; i++)
        new_user_pd[i] = boot_pdx[i] & ~(pt::uint64_t)0x04;

    pt::uintptr_t new_pt_frames[Task::MAX_PRIV_PTS];
    for (pt::size_t k = 0; k < num_pts; k++) {
        new_pt_frames[k] = vmm.allocate_frame();
        memset(reinterpret_cast<void*>(new_pt_frames[k]), 0, 4096);
    }

    // 3. Copy ELF frames from staging; populate code PTs with W^X permissions.
    for (pt::size_t i = 0; i < num_frames; i++) {
        pt::uintptr_t frame = vmm.allocate_frame();
        memcpy(reinterpret_cast<void*>(frame),
               reinterpret_cast<void*>(ELF_STAGING_VA + i * 4096),
               4096);
        pt::uint8_t pflags = ElfLoader::page_flags[i];
        pt::uint64_t pte = frame | 0x05;  // Present + User
        if (pflags & PF_W) pte |= 0x02;
        if (!(pflags & PF_X)) pte |= PTE_NX;
        pt::uint64_t* new_pt = reinterpret_cast<pt::uint64_t*>(new_pt_frames[i / 512]);
        new_pt[i % 512] = pte;
    }
    for (pt::size_t k = 0; k < num_pts; k++)
        new_user_pd[USER_CODE_PD_IDX + k] = new_pt_frames[k] | 0x07;

    // 4. Allocate stack PT and stack frames; wire into new_user_pd[USER_STACK_PD_IDX].
    pt::uintptr_t new_stack_pt_frame = vmm.allocate_frame();
    pt::uint64_t* new_stack_pt = reinterpret_cast<pt::uint64_t*>(new_stack_pt_frame);
    memset(new_stack_pt, 0, 4096);
    for (pt::size_t i = 0; i < USER_STACK_PAGES; i++) {
        pt::uintptr_t frame = vmm.allocate_frame();
        memset(reinterpret_cast<void*>(frame), 0, 4096);
        new_stack_pt[i] = frame | 0x07 | PTE_NX;  // RW + User + No-Execute
    }
    new_user_pd[USER_STACK_PD_IDX] = new_stack_pt_frame | 0x07;

    // 5. Wire user_pdpt[0] → user_pd.
    new_user_pdpt[USER_PDPT_IDX] = new_user_pd_frame | 0x07;

    // 6. Free old user page tables (code, stack, heap, user_pd, user_pdpt).
    //    Still under kernel_cr3 — direct PA access OK.
    free_user_pagetables(current);

    // 7. Install new page tables in task struct and PML4.
    current->user_pdpt    = new_user_pdpt_frame;
    current->user_pd      = new_user_pd_frame;
    current->stack_pt     = new_stack_pt_frame;
    current->user_heap_top = USER_HEAP_BASE;
    current->num_priv_pts = num_pts;
    for (pt::size_t k = 0; k < num_pts; k++) current->priv_pt[k] = new_pt_frames[k];
    for (pt::size_t k = num_pts; k < Task::MAX_PRIV_PTS; k++) current->priv_pt[k] = 0;

    pt::uint64_t* current_pml4 = reinterpret_cast<pt::uint64_t*>(current->cr3);
    current_pml4[USER_PML4_IDX] = new_user_pdpt_frame | 0x07;
    current_pml4[ELF_PML4_IDX] &= ~(pt::uint64_t)0x04;  // clear U bit on kernel half

    // 8. Build initial user RSP with argv + envp layout.
    //    Stack frames are at physical addresses under kernel_cr3 — direct PA access OK.
    //    For user VA `va` in [USER_STACK_BOT, USER_STACK_TOP):
    //      pt_idx = (va - USER_STACK_BOT) >> 12
    //      byte ptr = (new_stack_pt[pt_idx] & ~0xFFF) + (va & 0xFFF)
    // Layout (high→low):
    //   [string data: argv strings + env strings]
    //   [alignment]
    //   [NULL]  ← envp sentinel
    //   [envp[0..m-1]]
    //   [NULL]  ← argv sentinel
    //   [argv[0..n-1]]
    //   [argc]  ← final RSP
    pt::uint64_t new_user_rsp = USER_STACK_TOP - 16;

    // Helper lambda: write a byte at user VA via physical address.
    auto write_byte = [&](pt::uint64_t va, pt::uint8_t b) {
        pt::size_t pt_idx = (va - USER_STACK_BOT) >> 12;
        pt::uintptr_t pa = (new_stack_pt[pt_idx] & PTE_ADDR_MASK) + (va & 0xFFF);
        *reinterpret_cast<pt::uint8_t*>(pa) = b;
    };
    auto write_qword = [&](pt::uint64_t va, pt::uint64_t val) {
        pt::size_t pt_idx = (va - USER_STACK_BOT) >> 12;
        pt::uintptr_t pa = (new_stack_pt[pt_idx] & PTE_ADDR_MASK) + (va & 0xFFF);
        *reinterpret_cast<pt::uint64_t*>(pa) = val;
    };

    {
        pt::uint64_t rsp = USER_STACK_TOP;

        // Count env strings from current task's env_buf (flat "K=V\0K=V\0\0").
        const int ENV_MAX = 16;
        const char* env_strs[ENV_MAX];
        int envc = 0;
        {
            const char* p = current->env_buf;
            const char* end = current->env_buf + current->env_buf_len;
            while (p < end && *p && envc < ENV_MAX) {
                env_strs[envc++] = p;
                while (p < end && *p) p++;
                p++;  // skip NUL
            }
        }

        // Write env string data downward, record user-space pointers.
        pt::uint64_t uenv_ptrs[ENV_MAX];
        for (int i = envc - 1; i >= 0; i--) {
            const char* s = env_strs[i];
            int len = 0; while (s[len]) len++;
            len++;
            rsp -= (pt::uint64_t)len;
            if (rsp < USER_STACK_BOT) { rsp += (pt::uint64_t)len; break; }
            for (int j = 0; j < len; j++)
                write_byte(rsp + (pt::uint64_t)j, (pt::uint8_t)s[j]);
            uenv_ptrs[i] = rsp;
        }

        // Write argv string data downward, record user-space pointers.
        pt::uint64_t uarg_ptrs[ARGV_MAX_LOCAL];
        for (int i = real_argc - 1; i >= 0; i--) {
            const char* s = arg_kptrs[i];
            int len = 0; while (s[len]) len++;
            len++;
            rsp -= (pt::uint64_t)len;
            if (rsp < USER_STACK_BOT) { rsp += (pt::uint64_t)len; break; }
            for (int j = 0; j < len; j++)
                write_byte(rsp + (pt::uint64_t)j, (pt::uint8_t)s[j]);
            uarg_ptrs[i] = rsp;
        }

        // Align rsp down to 8 bytes.
        rsp &= ~(pt::uint64_t)7;

        // Total qwords to push: 1(argc) + real_argc + 1(NULL) + envc + 1(NULL)
        {
            pt::uint64_t total_qw = (pt::uint64_t)(real_argc + envc + 3);
            pt::uint64_t final_rsp = rsp - total_qw * 8;
            if (final_rsp & 0xF)
                rsp -= 8;
        }

        // Write envp NULL sentinel.
        rsp -= 8;
        if (rsp >= USER_STACK_BOT) write_qword(rsp, 0);

        // Write envp pointer array (reverse order).
        for (int i = envc - 1; i >= 0; i--) {
            rsp -= 8;
            if (rsp < USER_STACK_BOT) { rsp += 8; break; }
            write_qword(rsp, uenv_ptrs[i]);
        }

        // Write argv NULL sentinel.
        rsp -= 8;
        if (rsp >= USER_STACK_BOT) write_qword(rsp, 0);

        // Write argv pointer array (reverse order).
        for (int i = real_argc - 1; i >= 0; i--) {
            rsp -= 8;
            if (rsp < USER_STACK_BOT) { rsp += 8; break; }
            write_qword(rsp, uarg_ptrs[i]);
        }

        // Write argc qword.
        rsp -= 8;
        if (rsp >= USER_STACK_BOT) {
            write_qword(rsp, (pt::uint64_t)real_argc);
            new_user_rsp = rsp;
        }
    }

    // 9. Reset FPU/SSE state for the new program (clean slate, not inherited
    //    from the pre-exec image which may have changed rounding mode etc.).
    memcpy(current->fxsave_area, default_fxsave, 512);

    // 10. Patch the live iretq frame in-place.
    pt::uint64_t* frame = reinterpret_cast<pt::uint64_t*>(syscall_frame_rsp);
    frame[15] = entry;        // [+120] = new RIP
    frame[18] = new_user_rsp; // [+144] = new user RSP

    // 11. Switch back to task CR3 (now wired to new user address space) and flush TLB.
    // Update task name to the new binary (basename only).
    {
        const char* base = fname_buf;
        for (const char* p = fname_buf; *p; p++)
            if (*p == '/') base = p + 1;
        pt::size_t i = 0;
        for (; i < sizeof(current->name) - 1 && base[i]; i++)
            current->name[i] = base[i];
        current->name[i] = '\0';
    }

    // 12. Flush stale keyboard events so the new program starts clean.
    flush_key_events();

    klog("[EXEC] Task %d: iretq -> %lx rsp=%lx\n", current->id, entry, new_user_rsp);
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

    return 0;
}

// ─── map_user_pages ────────────────────────────────────────────────────────────
//
// Map [va, va+size) into the task's user address space by allocating physical
// frames and inserting them into the per-task user_pd hierarchy.
//
// Called from SYS_MMAP with the task's CR3 active.  Physical frames are NOT
// in the user's identity range, so all PT accesses use KERNEL_OFFSET + PA.
//
void TaskScheduler::map_user_pages(Task* t, pt::uintptr_t va, pt::size_t size)
{
    if (!t->user_pd) return;
    pt::uint64_t* upd = reinterpret_cast<pt::uint64_t*>(KERNEL_OFFSET + t->user_pd);

    for (pt::uintptr_t addr = va; addr < va + size; addr += 4096) {
        pt::size_t pd_idx = (addr >> 21) & 0x1FF;
        pt::size_t pt_idx = (addr >> 12) & 0x1FF;

        // Allocate a new heap PT frame if this PD entry is empty or a 2MB huge page
        // (boot identity entry that must be replaced with a 4KB PT for heap use).
        if (!(upd[pd_idx] & 0x01) || (upd[pd_idx] & 0x80)) {
            pt::uintptr_t pt_frame = vmm.allocate_frame();
            pt::uint64_t* new_pt = reinterpret_cast<pt::uint64_t*>(
                KERNEL_OFFSET + pt_frame);
            memset(new_pt, 0, 4096);
            upd[pd_idx] = pt_frame | 0x07;
        }

        pt::uintptr_t pt_pa = upd[pd_idx] & PTE_ADDR_MASK;
        pt::uint64_t* hpt = reinterpret_cast<pt::uint64_t*>(KERNEL_OFFSET + pt_pa);
        if (!(hpt[pt_idx] & 0x01)) {
            pt::uintptr_t frame = vmm.allocate_frame();
            pt::uint64_t* fp = reinterpret_cast<pt::uint64_t*>(KERNEL_OFFSET + frame);
            memset(fp, 0, 4096);
            hpt[pt_idx] = frame | 0x07 | PTE_NX;  // RW + User + No-Execute
            asm volatile("invlpg [%0]" : : "r"(addr) : "memory");
        }
    }
}

// ─── dump_task_map ─────────────────────────────────────────────────────────────
//
// Print a compact memory map of a task's user address space to klog.
// Walks user_pd entries and prints each mapped region with page counts.
//
void TaskScheduler::dump_task_map(pt::uint32_t task_id)
{
    if (task_id >= MAX_TASKS) {
        vterm_printf("[MAP] invalid task id %d\n", (int)task_id);
        return;
    }
    Task* t = &tasks[task_id];
    if (t->state == TASK_DEAD) {
        vterm_printf("[MAP] task %d is dead\n", (int)task_id);
        return;
    }

    vterm_printf("-- task %d%s%s --\n", (int)task_id,
         t->name[0] ? " " : "", t->name);
    vterm_printf("  state=%d user=%d cr3=%lx\n",
         (int)t->state, (int)t->user_mode, t->cr3);

    if (!t->user_pd) {
        vterm_printf("  (kernel task — no user page tables)\n");
        return;
    }

    vterm_printf("  user_pdpt=%lx  user_pd=%lx  stack_pt=%lx\n",
         t->user_pdpt, t->user_pd, t->stack_pt);
    vterm_printf("  heap_top=%lx  num_code_pts=%d\n",
         t->user_heap_top, (int)t->num_priv_pts);

    pt::uint64_t* upd = reinterpret_cast<pt::uint64_t*>(KERNEL_OFFSET + t->user_pd);

    // Walk PD entries, coalescing contiguous regions of the same type.
    pt::uintptr_t region_start = 0;
    int region_pages = 0;
    const char* region_label = nullptr;

    for (int pd_i = 0; pd_i < 512; pd_i++) {
        if (!(upd[pd_i] & 0x01) || (upd[pd_i] & 0x80)) {
            // Not present or 2MB huge page (boot identity) — flush region.
            if (region_pages > 0) {
                vterm_printf("  %lx-%lx  %dK  %s\n", region_start,
                     region_start + (pt::uintptr_t)region_pages * 4096,
                     region_pages * 4, region_label);
                region_pages = 0;
            }
            continue;
        }

        // 4KB PT — count present entries.
        pt::uintptr_t pt_pa = upd[pd_i] & PTE_ADDR_MASK;
        pt::uint64_t* pt = reinterpret_cast<pt::uint64_t*>(KERNEL_OFFSET + pt_pa);
        int count = 0;
        for (int j = 0; j < 512; j++) {
            if (pt[j] & 0x01) count++;
        }
        if (count == 0) {
            if (region_pages > 0) {
                vterm_printf("  %lx-%lx  %dK  %s\n", region_start,
                     region_start + (pt::uintptr_t)region_pages * 4096,
                     region_pages * 4, region_label);
                region_pages = 0;
            }
            continue;
        }

        const char* label;
        if (pd_i == (int)USER_STACK_PD_IDX)
            label = "stack";
        else if (pd_i >= (int)USER_CODE_PD_IDX &&
                 pd_i <  (int)USER_CODE_PD_IDX + (int)t->num_priv_pts)
            label = "code";
        else
            label = "heap";

        // Extend current region if same label, otherwise flush and start new.
        if (region_label == label && region_pages > 0) {
            region_pages += count;
        } else {
            if (region_pages > 0) {
                vterm_printf("  %lx-%lx  %dK  %s\n", region_start,
                     region_start + (pt::uintptr_t)region_pages * 4096,
                     region_pages * 4, region_label);
            }
            region_start = (pt::uintptr_t)pd_i << 21;
            region_pages = count;
            region_label = label;
        }
    }
    if (region_pages > 0) {
        vterm_printf("  %lx-%lx  %dK  %s\n", region_start,
             region_start + (pt::uintptr_t)region_pages * 4096,
             region_pages * 4, region_label);
    }
}

// ─── mprotect_pages ───────────────────────────────────────────────────────────
//
// Change page permissions for [va, va+size) in the current task.
// prot bits: 0=exec, 1=write, 2=read (matches PROT_EXEC/PROT_WRITE/PROT_READ).
// Called with the task's CR3 active — accesses PTs via KERNEL_OFFSET + PA.
//
int TaskScheduler::mprotect_pages(pt::uintptr_t va, pt::size_t size, int prot)
{
    Task* t = get_current_task();
    if (!t || !t->user_pd) return -1;

    pt::uint64_t* upd = reinterpret_cast<pt::uint64_t*>(KERNEL_OFFSET + t->user_pd);

    for (pt::uintptr_t addr = va & ~(pt::uintptr_t)0xFFF;
         addr < va + size; addr += 4096) {
        pt::size_t pd_idx = (addr >> 21) & 0x1FF;
        pt::size_t pt_idx = (addr >> 12) & 0x1FF;

        if (!(upd[pd_idx] & 0x01) || (upd[pd_idx] & 0x80))
            continue;  // not mapped or boot huge page

        pt::uintptr_t pt_pa = upd[pd_idx] & PTE_ADDR_MASK;
        pt::uint64_t* pt = reinterpret_cast<pt::uint64_t*>(KERNEL_OFFSET + pt_pa);

        if (!(pt[pt_idx] & 0x01))
            continue;  // page not present

        // Preserve the physical frame address; rebuild permission flags.
        pt::uintptr_t frame = pt[pt_idx] & PTE_ADDR_MASK;
        pt::uint64_t new_pte = frame | 0x05;  // Present + User
        if (prot & 2) new_pte |= 0x02;        // Writable
        if (!(prot & 1)) new_pte |= PTE_NX;   // No-Execute

        pt[pt_idx] = new_pte;
        asm volatile("invlpg [%0]" : : "r"(addr) : "memory");
    }
    return 0;
}
