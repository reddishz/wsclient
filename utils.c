#include <sys/types.h>

#include "utils.h"

#if defined(__linux__) || defined(__GLIBC__)
#include <byteswap.h>
#else
#define __bswap_64(x)                      \
    x = (x >> 56) |                        \
        ((x << 40) & 0x00FF000000000000) | \
        ((x << 24) & 0x0000FF0000000000) | \
        ((x << 8) & 0x000000FF00000000) |  \
        ((x >> 8) & 0x00000000FF000000) |  \
        ((x >> 24) & 0x0000000000FF0000) | \
        ((x >> 40) & 0x000000000000FF00) | \
        (x << 56)
#endif

uint64_t hton64(uint64_t val)
{
    if (__BYTE_ORDER == __BIG_ENDIAN)
        return (val);
    else
        return __bswap_64(val);
}

uint64_t ntoh64(uint64_t val)
{
    if (__BYTE_ORDER == __BIG_ENDIAN)
        return (val);
    else
        return __bswap_64(val);
}