#pragma once
#include "defs.h"

class Shell;

class LineReader {
public:
    LineReader();

    // Process one input character, returns true if char was handled
    bool process(char c);

    // Check if a complete line is ready (Enter was pressed)
    [[nodiscard]] bool has_line() const;

    // Get the current line buffer
    [[nodiscard]] const char* get_line() const;

    // Clear the buffer and reset state
    void clear();

    // Set shell pointer for tab completion
    void set_shell(Shell* s) { shell_ptr = s; }

    static constexpr int BUFFER_SIZE = 128;

private:
    char buffer[BUFFER_SIZE];
    int pos;
    bool line_ready;
    Shell* shell_ptr;

    void echo(char c);
    void echo_backspace();
    void echo_newline();
    void handle_tab();
    void replace_line(const char* new_text);
};
