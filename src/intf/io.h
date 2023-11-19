#pragma once
#include "defs.h"

class IO
{
public:
    static inline void outb(uint16_t port, uint8_t value) {
        asm volatile("out dx, al" :: "a"(value), "d"(port));
    }

    static inline void outw(uint16_t port, uint16_t value) {
        asm volatile("out dx, ax" :: "a"(value), "d"(port));
    }

    static inline uint8_t inb(uint16_t port) {
        uint8_t ret;
        asm volatile("in al, dx" : "=a"(ret) : "d"(port));
        return ret;
    }

    static inline void io_wait(void)
    {
        /* TODO: This is probably fragile. */
        asm volatile ( "jmp 1f\n\t"
                    "1:jmp 2f\n\t"
                    "2:" );
    }

};