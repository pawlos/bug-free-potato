#pragma once

#include "defs.h"

struct PipeBuffer {
    static constexpr pt::uint32_t CAPACITY = 512;
    pt::uint8_t  data[CAPACITY];
    pt::uint32_t read_pos;       // monotonically increasing (mod CAPACITY for index)
    pt::uint32_t write_pos;      // monotonically increasing
    pt::uint32_t ref_count;      // number of open FD ends (2 initially; +1 per fork)
    bool         writer_closed;  // set when the WR end is closed → reader gets EOF
};

// Store/load a PipeBuffer* inside File::fs_data[0..7] via memcpy.
// This avoids strict-aliasing issues on unaligned reads.
static inline PipeBuffer* pipe_get_buf(const pt::uint8_t* fs_data) {
    PipeBuffer* p;
    __builtin_memcpy(&p, fs_data, sizeof(p));
    return p;
}

static inline void pipe_set_buf(pt::uint8_t* fs_data, PipeBuffer* p) {
    __builtin_memcpy(fs_data, &p, sizeof(p));
}
