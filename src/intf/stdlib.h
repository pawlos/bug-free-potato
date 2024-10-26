#pragma once
#include "defs.h"

void kclear(pt::uint8_t *ptr, pt::size_t size);
bool kmemcmp(const char *src, const char *dst, pt::size_t size);
void kmemcpy(pt::uintptr_t *dst, const pt::uintptr_t *src, pt::size_t size);
