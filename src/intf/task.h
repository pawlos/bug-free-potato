#pragma once
#include "defs.h"

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
    pt::uint32_t id;
    TaskState state;
    TaskContext context;        // legacy; kept for padding / future use
    pt::uintptr_t kernel_stack_base;
    pt::size_t kernel_stack_size;
    pt::uint64_t ticks_alive;
    // RSP pointing to the PUSHALL frame saved at the last interrupt boundary.
    // This is the value restored by irq0/int-0x81 to resume the task.
    pt::uintptr_t preempt_rsp;
};

class TaskScheduler {
public:
    static constexpr pt::size_t MAX_TASKS = 16;
    static constexpr pt::size_t TASK_STACK_SIZE = 4096;  // 4KB per task
    // How many timer ticks between forced preemptions.
    // At 50 Hz this gives ~200 ms time slices.
    static constexpr pt::size_t SCHEDULER_QUANTUM = 10;

    // Initialize scheduler
    static void initialize();

    // Create a new task with given entry function
    static pt::uint32_t create_task(void (*entry_fn)(), pt::size_t stack_size = TASK_STACK_SIZE);

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

    // Mark task as dead and switch away
    static void task_exit();

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
