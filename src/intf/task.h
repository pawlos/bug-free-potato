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
    TaskContext context;
    pt::uintptr_t kernel_stack_base;
    pt::size_t kernel_stack_size;
    pt::uint64_t ticks_alive;
};

class TaskScheduler {
public:
    static constexpr pt::size_t MAX_TASKS = 16;
    static constexpr pt::size_t TASK_STACK_SIZE = 4096;  // 4KB per task

    // Initialize scheduler
    static void initialize();

    // Create a new task with given entry function
    static pt::uint32_t create_task(void (*entry_fn)(), pt::size_t stack_size = TASK_STACK_SIZE);

    // Get current running task
    static Task* get_current_task();

    // Scheduler tick - called from timer interrupt
    static void scheduler_tick();

    // Task yield - voluntarily give up CPU
    static void task_yield();

    // Mark task as dead
    static void task_exit();

private:
    static Task tasks[MAX_TASKS];
    static pt::uint32_t task_count;
    static pt::uint32_t current_task_id;
    static pt::uint64_t scheduler_ticks;

    // Switch to next ready task
    static void switch_to_next_task();

    // Get task by ID
    static Task* get_task(pt::uint32_t id);
};

// Assembly functions for context switching
extern "C" {
    // Save current context and load new context
    void task_switch(TaskContext* old_context, TaskContext* new_context);
}
