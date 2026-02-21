#include "tss.h"
#include "virtual.h"
#include "kernel.h"

static TSS64 kernel_tss;

// GDT pseudo-descriptor read by sgdt.
struct GDTPointer {
    pt::uint16_t limit;
    pt::uint64_t base;
} __attribute__((packed));

void tss_init() {
    memset(&kernel_tss, 0, sizeof(TSS64));
    kernel_tss.iomap_base = sizeof(TSS64);  // no IO permission map

    // Read the current GDT base via sgdt.  The GDT is in low physical memory
    // (identity-mapped), so gdtr.base is its accessible virtual address too.
    GDTPointer gdtr;
    asm volatile("sgdt %0" : "=m"(gdtr));

    pt::uint64_t base  = (pt::uint64_t)&kernel_tss;
    pt::uint64_t limit = sizeof(TSS64) - 1;  // 103

    // Build the 16-byte x86-64 system segment descriptor.
    // Low qword layout:
    //   bits 15: 0  = limit[15:0]
    //   bits 31:16  = base[15:0]
    //   bits 39:32  = base[23:16]
    //   bits 47:40  = 0x89  (P=1, DPL=0, type=9 TSS-available)
    //   bits 51:48  = limit[19:16]
    //   bits 55:52  = 0
    //   bits 63:56  = base[31:24]
    pt::uint64_t desc_low =
        (limit & 0xFFFF) |
        ((base  & 0xFFFF)        << 16) |
        (((base  >> 16) & 0xFF)  << 32) |
        (0x89ULL                 << 40) |
        (((limit >> 16) & 0xF)  << 48) |
        (((base  >> 24) & 0xFF) << 56);

    // High qword: base[63:32] in bits 31:0, rest reserved.
    pt::uint64_t desc_high = (base >> 32) & 0xFFFFFFFF;

    // Write descriptor into GDT at offset 0x28 (= 5 × 8-byte entries).
    pt::uint64_t* tss_slot = reinterpret_cast<pt::uint64_t*>(gdtr.base + 0x28);
    tss_slot[0] = desc_low;
    tss_slot[1] = desc_high;

    // Load Task Register with selector 0x28.
    asm volatile("ltr ax" : : "a"((pt::uint16_t)0x28));

    klog("[TSS] Initialized, base=%lx\n", base);
}

void tss_set_rsp0(pt::uintptr_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}
