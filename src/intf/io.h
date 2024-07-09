#pragma once
#include "defs.h"

class IO
{
public:
    static inline void outb(pt::uint16_t port, pt::uint8_t value) {
        asm volatile("out dx, al" :: "a"(value), "d"(port));
    }

    static inline void outw(pt::uint16_t port, pt::uint16_t value) {
        asm volatile("out dx, ax" :: "a"(value), "d"(port));
    }

    static inline pt::uint8_t inb(pt::uint16_t port) {
        pt::uint8_t ret;
        asm volatile("in al, dx" : "=a"(ret) : "d"(port));
        return ret;
    }

    static inline void io_wait()
    {
        /* TODO: This is probably fragile. */
        asm volatile ( "jmp 1f\n\t"
                    "1:jmp 2f\n\t"
                    "2:" );
    }

};