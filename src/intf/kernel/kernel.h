#pragma once

inline void outb(uint16_t port, uint8_t value) {
	asm volatile("out dx, al" :: "a"(value), "d"(port));
}

inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	asm volatile("in al, dx" : "=a"(ret) : "d"(port));
	return ret;
}