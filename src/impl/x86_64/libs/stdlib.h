#pragma once
#include "defs.h"

void clear(pt::uint8_t *ptr, pt::size_t size);
bool memcmp(const char *src, const char *dst, const pt::size_t size);
pt::size_t parse_decimal(const char *str);
