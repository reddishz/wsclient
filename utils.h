
#ifndef _UTILS_H_
#define _UTILS_H_
#include <stdint.h>

#define TEST_FLAG(s, f) (s->flags & f)

#define LIBWSCLIENT_ON_ERROR(ws, info) \
    {                                  \
        if (ws->onerror)               \
        {                              \
            ws->onerror(ws, 1, info);  \
        }                              \
    }
#define LIBWSCLIENT_ON_INFO(ws, info) \
    {                                 \
        if (ws->onerror)              \
        {                             \
            ws->onerror(ws, 0, info); \
        }                             \
    }
uint64_t hton64(uint64_t val);
uint64_t ntoh64(uint64_t val);

int base64_encode(unsigned char *source, size_t sourcelen, char *target, size_t targetlen);

#endif
