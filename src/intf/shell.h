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
    static constexpr int CMD_BUFFER_SIZE = 16;
    char history[MAX_HISTORY][CMD_BUFFER_SIZE];
    int history_count;
    int history_index;
};

extern Shell shell;
