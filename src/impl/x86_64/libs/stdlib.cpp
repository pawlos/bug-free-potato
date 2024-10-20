#include "stdlib.h"

void clear(pt::uintptr_t *ptr, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        *ptr = 0;
    }
}

bool memcmp(const char *src, const char *dst, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        const char src_char = *src;
        if (const char dst_char = *dst; src_char != dst_char) {
            return false;
        }
        src++;
        dst++;
    }
    return true;
}