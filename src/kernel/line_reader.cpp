#include "line_reader.h"
#include "shell.h"
#include "kernel.h"
#include "vterm.h"

LineReader::LineReader() : pos(0), line_ready(false), shell_ptr(nullptr) {
    buffer[0] = '\0';
}

void LineReader::echo(char c) {
    vterm_printf("%c", c);
}

void LineReader::echo_backspace() {
    vterm_printf("\b \b");
}

void LineReader::echo_newline() {
    vterm_printf("\n");
}

void LineReader::replace_line(const char* new_text) {
    // Erase current display
    while (pos > 0) {
        echo_backspace();
        pos--;
    }
    // Copy new text into buffer and echo it
    int i = 0;
    while (new_text[i] && i < BUFFER_SIZE - 1) {
        buffer[i] = new_text[i];
        echo(buffer[i]);
        i++;
    }
    buffer[i] = '\0';
    pos = i;
}

void LineReader::handle_tab() {
    if (!shell_ptr) return;

    static char matches[16][128];
    int n = shell_ptr->complete(buffer, pos, matches, 16);
    if (n == 0) return;

    if (n == 1) {
        // Single match — replace entire line with it
        replace_line(matches[0]);
    } else {
        // Multiple matches — find longest common prefix
        int prefix_len = 0;
        while (true) {
            char ch = matches[0][prefix_len];
            if (ch == '\0') break;
            bool all_match = true;
            for (int i = 1; i < n; i++) {
                if (matches[i][prefix_len] != ch) {
                    all_match = false;
                    break;
                }
            }
            if (!all_match) break;
            prefix_len++;
        }

        if (prefix_len > pos) {
            // First tab: silently extend to common prefix (bash-style)
            while (pos > 0) { echo_backspace(); pos--; }
            for (int i = 0; i < prefix_len && i < BUFFER_SIZE - 1; i++) {
                buffer[i] = matches[0][i];
                echo(buffer[i]);
            }
            buffer[prefix_len] = '\0';
            pos = prefix_len;
        } else {
            // Second tab: common prefix matches current input, show options
            vterm_printf("\n");
            for (int i = 0; i < n; i++) {
                const char* name = matches[i];
                int nlen = 0;
                while (name[nlen]) nlen++;
                // Find display portion: skip past cmd prefix, ignore trailing '/'
                int search_end = (nlen > 0 && name[nlen - 1] == '/') ? nlen - 1 : nlen;
                const char* display = name;
                for (int j = 0; j < search_end; j++) {
                    if (name[j] == '/' || name[j] == ' ') display = name + j + 1;
                }
                vterm_printf("  %s", display);
                if (i + 1 < n) vterm_printf("  ");
            }
            vterm_printf("\n");

            // Reprint prompt + current buffer
            const char* cwd_str = shell_ptr->get_cwd();
            if (cwd_str[0])
                vterm_printf("potatOS:/%s> ", cwd_str);
            else
                vterm_printf("potatOS:/> ");
            for (int i = 0; i < pos; i++)
                echo(buffer[i]);
        }
    }
}

bool LineReader::process(char c) {
    // No input available
    if (c == -1) {
        return false;
    }

    // Enter key - line is complete
    if (c == '\n') {
        echo_newline();
        line_ready = true;
        return true;
    }

    // Tab key - attempt completion
    if (c == '\t') {
        handle_tab();
        return true;
    }

    // Backspace
    if (c == '\b') {
        if (pos > 0) {
            pos--;
            buffer[pos] = '\0';
            echo_backspace();
        }
        return true;
    }

    // Regular character
    if (pos < BUFFER_SIZE - 1) {
        buffer[pos++] = c;
        buffer[pos] = '\0';
        echo(c);
        return true;
    }

    // Buffer full, ignore character
    return false;
}

bool LineReader::has_line() const {
    return line_ready;
}

const char* LineReader::get_line() const {
    return buffer;
}

void LineReader::clear() {
    pos = 0;
    buffer[0] = '\0';
    line_ready = false;
}
