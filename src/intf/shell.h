#pragma once
#include "defs.h"

class Shell {
public:
    Shell();
    bool execute(const char* cmd);
    void add_to_history(const char* cmd);
    void print_history();
    
private:
    static constexpr int MAX_HISTORY = 10;
    static constexpr int CMD_BUFFER_SIZE = 128;
    char history[MAX_HISTORY][CMD_BUFFER_SIZE];
    int history_count;
    int history_index;

    // Command handlers
    void execute_mem(const char* cmd);
    void execute_ticks(const char* cmd);
    void execute_alloc(const char* cmd);
    void execute_clear_color(const char* cmd, pt::uint8_t r, pt::uint8_t g, pt::uint8_t b);
    void execute_vmm(const char* cmd);
    void execute_pci(const char* cmd);
    void execute_map(const char* cmd);
    void execute_history(const char* cmd);
    void execute_ls(const char* cmd);
    void execute_cat(const char* cmd);
    void execute_play(const char* cmd);
    void execute_disk(const char* cmd);
    void execute_echo(const char* cmd);
    void execute_clear(const char* cmd);
    void execute_timers(const char* cmd);
    void execute_cancel(const char* cmd);
    void execute_shutdown(const char* cmd);
    void execute_reboot(const char* cmd);
    void execute_task(const char* cmd);
    void execute_help(const char* cmd);
    void execute_write(const char* cmd);
    void execute_rm(const char* cmd);
};

extern Shell shell;
