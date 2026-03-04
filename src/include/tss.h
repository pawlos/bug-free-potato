#pragma once
#include "defs.h"

// Minimal x86-64 Task State Segment (104 bytes).
struct TSS64 {
    pt::uint32_t reserved0;
    pt::uint64_t rsp0;   // kernel RSP loaded by CPU on ring-3 → ring-0 transition
    pt::uint64_t rsp1;
    pt::uint64_t rsp2;
    pt::uint64_t reserved1;
    pt::uint64_t ist[7]; // IST stack pointers (unused)
    pt::uint64_t reserved2;
    pt::uint16_t reserved3;
    pt::uint16_t iomap_base; // set to sizeof(TSS64) → no IO permission map
} __attribute__((packed));

// Fill the GDT TSS descriptor and load TR with selector 0x28.
// Must be called after the GDT is loaded (i.e., after long_mode_start).
void tss_init();

// Update TSS.RSP0 — called on every switch to a user-mode task so the CPU
// uses the correct kernel stack when the next interrupt fires from ring-3.
void tss_set_rsp0(pt::uintptr_t rsp0);
