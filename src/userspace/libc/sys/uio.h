#pragma once
/* Minimal stub: potatOS has no scatter/gather I/O. Provided so that ports
 * which include <sys/uio.h> as part of their POSIX boilerplate (e.g.,
 * Wolf4SDL's id_ca.cpp) don't fall through to /usr/include and trip on
 * host-specific typedefs. */

#include "types.h"

struct iovec {
    void  *iov_base;
    size_t iov_len;
};
