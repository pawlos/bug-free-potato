#include "../../../intf/stdlib.h"

void kclear(pt::uint8_t *ptr, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        *ptr = 0;
    }
}

bool kmemcmp(const char *src, const char *dst, const pt::size_t size) {
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

void kmemcpy(pt::uint8_t *dst, const pt::uint8_t *src, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        *dst = *src;
        src++;
        dst++;
    }
}