#pragma once
#include "defs.h"
#include "vfs.h"

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

    // Private page-table frames carved out for user ELF code isolation.
    // Each user ELF task gets its own PDPT/PD/PT so that the code region
    // (0xFFFF800018000000) maps to task-private physical frames, allowing
    // multiple ELF tasks to run concurrently without overwriting each other.
    // All three are 0 for kernel tasks or tasks created without create_elf_task.
    pt::uintptr_t priv_pdpt;   // physical addr of private PDPT frame (or 0)
    pt::uintptr_t priv_pd;     // physical addr of private PD frame   (or 0)
    pt::uintptr_t priv_pt;     // physical addr of private PT frame   (or 0)

    // Process hierarchy and exit status (for fork/waitpid).
    pt::uint32_t parent_id;    // 0xFFFFFFFF = no parent
    pt::uint32_t waiting_for;  // child task ID we're blocked on; 0xFFFFFFFF = none
    int  exit_code;    // populated by task_exit() / SYS_EXIT

    // Timed sleep: absolute tick deadline set by sleep_task().
    // 0 means the task is not sleeping (default).
    pt::uint64_t sleep_deadline;

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

class TaskScheduler {
public:
    static constexpr pt::size_t MAX_TASKS = 16;
    static constexpr pt::size_t TASK_STACK_SIZE = 4096;   // 4KB kernel interrupt stack
    static constexpr pt::size_t USER_STACK_SIZE  = 16384; // 16KB user execution stack
    // How many timer ticks between forced preemptions.
    // At 50 Hz this gives ~200 ms time slices.
    static constexpr pt::size_t SCHEDULER_QUANTUM = 10;

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
    static pt::uint64_t exec_task(const char* filename,
                                  pt::uintptr_t syscall_frame_rsp);

    // waitpid_task: block until child exits; writes its exit code.
    // Returns 0 on success, (uint64_t)-1 on invalid child_id / not a child.
    static pt::uint64_t waitpid_task(pt::uint32_t child_id,
                                     int* out_exit_code);

    // Block the current task for at least ms milliseconds, then resume.
    // Returns immediately if ms == 0.
    static void sleep_task(pt::uint64_t ms);

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
