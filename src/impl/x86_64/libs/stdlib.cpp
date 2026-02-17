#include "stdlib.h"

void clear(pt::uint8_t *ptr, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        *ptr = 0;
        ptr++;
    }
}

bool memcmp(const char *src, const char *dst, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        const char src_char = *src;
        const char dst_char = *dst;
        if (src_char != dst_char) {
            return false;
        }
        src++;
        dst++;
    }
    return true;
}

pt::size_t parse_decimal(const char *str) {
    pt::size_t result = 0;

    // Skip leading spaces
    while (*str == ' ' || *str == '\t') {
        str++;
    }

    // Parse decimal digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result;
}
