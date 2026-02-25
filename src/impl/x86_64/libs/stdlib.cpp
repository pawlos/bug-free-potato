#include "stdlib.h"

void clear(pt::uint8_t *ptr, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        *ptr = 0;
        ptr++;
    }
}

int memcmp(const char *src, const char *dst, const pt::size_t size) {
    for (pt::size_t i = 0; i < size; i++) {
        const char src_char = *src++;
        const char dst_char = *dst++;
        if (src_char != dst_char)
            return (unsigned char)src_char - (unsigned char)dst_char;
    }
    return 0;
}

pt::size_t parse_decimal(const char *str) {
    pt::size_t result = 0;

    // Skip leading spaces
    while (*str == ' ' || *str == '\t') {
        str++;
    }

    // Parse decimal digits; clamp to SIZE_MAX on overflow
    while (*str >= '0' && *str <= '9') {
        pt::size_t digit = (pt::uint8_t)(*str - '0');
        if (result > ((pt::size_t)-1 - digit) / 10)
            return (pt::size_t)-1;  // would overflow: clamp
        result = result * 10 + digit;
        str++;
    }

    return result;
}
