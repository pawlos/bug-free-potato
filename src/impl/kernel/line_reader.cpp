#include "line_reader.h"
#include "kernel.h"

LineReader::LineReader() : pos(0), line_ready(false) {
    buffer[0] = '\0';
}

void LineReader::echo(char c) {
    klog("%c", c);
}

void LineReader::echo_backspace() {
    klog("\b \b");
}

void LineReader::echo_newline() {
    klog("\n");
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
