#pragma once
#include "defs.h"
#include "fs/vfs.h"
#include "vterm.h"

// Task states
enum TaskState {
    TASK_READY = 0,
    TASK_RUNNING = 1,
    TASK_BLOCKED = 2,
    TASK_DEAD = 3
};

// Saved CPU context for a task
struct TaskContext {
    pt::uint64_t rax;
    pt::uint64_t rbx;
    pt::uint64_t rcx;
    pt::uint64_t rdx;
    pt::uint64_t rsi;
    pt::uint64_t rdi;
    pt::uint64_t rbp;
    pt::uint64_t r8;
    pt::uint64_t r9;
    pt::uint64_t r10;
    pt::uint64_t r11;
    pt::uint64_t r12;
    pt::uint64_t r13;
    pt::uint64_t r14;
    pt::uint64_t r15;
    pt::uint64_t rsp;
    pt::uint64_t rip;
    pt::uint64_t rflags;
} __attribute__((packed));

// Task control block
struct Task {
    static constexpr pt::size_t MAX_FDS = 8;

    pt::uint32_t id;
    TaskState state;
    TaskContext context;        // legacy; kept for padding / future use
    pt::uintptr_t kernel_stack_base;
    pt::size_t kernel_stack_size;
    pt::uint64_t ticks_alive;
    // RSP pointing to the PUSHALL frame saved at the last interrupt boundary.
    // This is the value restored by irq0/int-0x81 to resume the task.
    pt::uintptr_t preempt_rsp;
    // Physical address of this task's PML4 page table.
    // 0 = unset; loaded into CR3 on every context switch.
    pt::uintptr_t cr3;
    // True if this task runs at CPL=3 (ring-3). The synthetic iretq frame
    // uses user CS=0x1B / SS=0x23 and TSS.RSP0 is updated on every switch.
    bool user_mode;
    // Separate user-mode execution stack (ring-3 tasks only).
    // The kernel_stack is used exclusively for interrupt/syscall frames
    // (TSS RSP0).  Keeping them separate prevents the interrupt frame from
    // overwriting the user's call frames when a syscall fires.
    // 0 for kernel-mode tasks.
    pt::uintptr_t user_stack_base;
    // Per-task open file descriptors.  slot.open==false means free.
    File fd_table[MAX_FDS];

    // Per-task user-space page-table frames.
    // user_pdpt/user_pd are fresh (not boot-table copies), wired at PML4[0].
    // priv_pt[] holds code PT frames; stack_pt holds the 2MB stack PT.
    // All zero for kernel tasks or tasks without create_elf_task.
    // MAX_PRIV_PTS=8 covers up to 8×512×4KB = 16 MB of ELF code.
    static constexpr pt::size_t MAX_PRIV_PTS = 8;
    pt::uintptr_t user_pdpt;                  // PA of per-task user PDPT (PML4[0] target)
    pt::uintptr_t user_pd;                    // PA of per-task user PD   (PDPT[0] target)
    pt::uintptr_t priv_pt[MAX_PRIV_PTS];      // PA of code PT frames
    pt::size_t    num_priv_pts;               // how many priv_pt[] entries are valid
    pt::uintptr_t stack_pt;                   // PA of stack PT frame  (PD[511] target)
    pt::uintptr_t user_heap_top;              // next free user heap VA (for SYS_MMAP)

    // Process hierarchy and exit status (for fork/waitpid).
    pt::uint32_t parent_id;    // 0xFFFFFFFF = no parent
    pt::uint32_t waiting_for;  // child task ID we're blocked on; 0xFFFFFFFF = none
    int  exit_code;    // populated by task_exit() / SYS_EXIT

    // Timed sleep: absolute tick deadline set by sleep_task().
    // 0 means the task is not sleeping (default).
    pt::uint64_t sleep_deadline;

    // Window ID owned by this task; INVALID_WID (0xFFFFFFFF) = no window.
    pt::uint32_t window_id;

    // VTerm ID bound to this task; INVALID_VT (0xFFFFFFFF) = not bound.
    pt::uint32_t vterm_id;

    // 512-byte FXSAVE area for x87/SSE state (must be 16-byte aligned).
    // Saved/restored by the scheduler on every context switch so tasks
    // don't corrupt each other's floating-point state.
    pt::uint8_t fxsave_area[512] __attribute__((aligned(16)));

    // Per-task snapshot of g_syscall_rsp captured at the START of every syscall
    // handler invocation (before any blocking that would let other tasks
    // overwrite the global).  fork_task and exec_task use this instead of the
    // global so that a stale g_syscall_rsp (overwritten by a child's SYS_EXIT
    // during waitpid blocking) cannot corrupt the parent's iretq frame patch.
    pt::uintptr_t syscall_frame_rsp;
};

// Kernel RSP captured in _syscall_stub immediately after PUSHALL.
// Points to the PUSHALL+iretq frame; used by fork_task and exec_task
// to inspect/clone/patch the calling user task's register state.
extern pt::uintptr_t g_syscall_rsp;

// Pending CR3 value to load in the timer/yield assembly stub after RSP is
// already switched to the new task's kernel stack.  Set by do_switch_to_next;
// cleared by the assembly after loading.  0 = no switch pending.
// Must be declared extern so idt.asm can reference it via [extern g_next_cr3].
extern pt::uintptr_t g_next_cr3;

class TaskScheduler {
public:
    static constexpr pt::size_t MAX_TASKS = 16;
    static constexpr pt::size_t TASK_STACK_SIZE = 16384;  // 16KB kernel interrupt stack
    static constexpr pt::size_t USER_STACK_SIZE  = 2097152; // 2MB user execution stack
    // How many timer ticks between forced preemptions.
    // At 50 Hz this gives ~200 ms time slices.
    static constexpr pt::size_t SCHEDULER_QUANTUM = 10;

    // User-space virtual address layout constants.
    static constexpr pt::uintptr_t USER_CODE_BASE    = 0x0000000000400000ULL; // ELF load base
    static constexpr pt::uintptr_t USER_HEAP_BASE    = 0x0000000001000000ULL; // heap start
    static constexpr pt::uintptr_t USER_STACK_BOT    = 0x000000003FE00000ULL; // PD[511] start
    static constexpr pt::uintptr_t USER_STACK_TOP    = 0x0000000040000000ULL; // initial RSP
    static constexpr pt::size_t    USER_PML4_IDX     = 0;    // PML4 index for user space
    static constexpr pt::size_t    USER_PDPT_IDX     = 0;    // PDPT index for first 1GB
    static constexpr pt::size_t    USER_CODE_PD_IDX  = 2;    // 0x400000 / 2MB = 2
    static constexpr pt::size_t    USER_STACK_PD_IDX = 511;  // 0x3FE00000 / 2MB = 511
    static constexpr pt::size_t    USER_STACK_PAGES  = 512;  // 2MB / 4KB

    // Initialize scheduler
    static void initialize();

    // Create a new task with given entry function.
    // Pass user_mode=true to start the task at CPL=3 (ring-3).
    static pt::uint32_t create_task(void (*entry_fn)(),
                                    pt::size_t stack_size = TASK_STACK_SIZE,
                                    bool user_mode = false);

    // Get current running task
    static Task* get_current_task();

    // Called from irq0_schedule (timer interrupt boundary).
    // Saves current task's context, may switch to next task.
    // Returns the RSP to resume (same or different task).
    static pt::uintptr_t preempt(pt::uintptr_t rsp);

    // Called from yield_schedule (int 0x81 boundary).
    // Always tries to switch to the next ready task.
    static pt::uintptr_t yield_tick(pt::uintptr_t rsp);

    // Task yield - voluntarily give up CPU (fires int 0x81)
    static void task_yield();

    // Mark task as dead and switch away; stores exit_code in the task slot
    // and wakes any parent blocked in waitpid_task().
    static void task_exit(int exit_code = 0);

    // Load an ELF file and start it as a new user-mode task with private
    // page-table frames so it can coexist with other running ELF tasks.
    // Returns the new task ID, or 0xFFFFFFFF on failure.
    static pt::uint32_t create_elf_task(const char* filename);

    // Kill all user-mode tasks (used by exec before loading a new ELF).
    // Resources are freed immediately; the caller must not be a user task.
    static void kill_user_tasks();

    // fork_task: clone the current task into a new slot.
    // syscall_frame_rsp points to the PUSHALL+iretq frame on the kernel stack.
    // Returns child task ID to parent; child frame gets rax=0.
    static pt::uint32_t fork_task(pt::uintptr_t syscall_frame_rsp);

    // exec_task: replace current task's ELF image with filename.
    // Patches the live iretq frame in place; returns 0 on success, -1 on error.
    // argc/argv (optional) are placed on the new user stack per the SysV ABI.
    static pt::uint64_t exec_task(const char* filename,
                                  pt::uintptr_t syscall_frame_rsp,
                                  int argc = 0,
                                  const char* const* argv = nullptr);

    // waitpid_task: block until child exits; writes its exit code.
    // Returns 0 on success, (uint64_t)-1 on invalid child_id / not a child.
    static pt::uint64_t waitpid_task(pt::uint32_t child_id,
                                     int* out_exit_code);

    // Block the current task for at least ms milliseconds, then resume.
    // Returns immediately if ms == 0.
    static void sleep_task(pt::uint64_t ms);

    // Map [va, va+size) into the task's user address space by allocating
    // physical frames and inserting them into the task's user_pd hierarchy.
    // Called from the SYS_MMAP handler (task CR3 active — uses KERNEL_OFFSET).
    static void map_user_pages(Task* t, pt::uintptr_t va, pt::size_t size);

    // Dump a compact memory map of a task's user address space to klog.
    // Walks user_pd entries and prints contiguous mapped regions.
    static void dump_task_map(pt::uint32_t task_id);

private:
    static Task tasks[MAX_TASKS];
    static pt::uint32_t task_count;
    static pt::uint32_t current_task_id;
    static pt::uint64_t scheduler_ticks;

    // Round-robin: find next ready task and switch to it.
    // Returns new preempt_rsp (or current_rsp if no switch).
    static pt::uintptr_t do_switch_to_next(pt::uintptr_t current_rsp);

    // Get task by ID
    static Task* get_task(pt::uint32_t id);

    // irq0_schedule and yield_schedule are C-linkage functions in idt.cpp
    // that call preempt() and yield_tick() respectively.
    friend pt::uintptr_t irq0_schedule_impl(pt::uintptr_t);
    friend pt::uintptr_t yield_schedule_impl(pt::uintptr_t);
};
