/**
 * This file is a subset of libctru's console.c, stripped down to the minimum
 * required to set up stderr to be sent over GDB stub.
 */

#include <stdio.h>

#include <sys/iosupport.h>

// clang-format off
#include <3ds/types.h>
#include <3ds/console.h>
#include <3ds/svc.h>
// clang-format on

static ssize_t debug_write(struct _reent* r, void* fd, const char* ptr,
                           size_t len) {
    svcOutputDebugString(ptr, len);
    return len;
}

static const devoptab_t dotab_svc = {
    .name = "svc",
    .structSize = 0,
    .write_r = debug_write,
};

void consoleDebugInit(debugDevice device) {
    int buffertype = _IONBF;

    switch (device) {
    case debugDevice_SVC:
        devoptab_list[STD_ERR] = &dotab_svc;
        buffertype = _IOLBF;
        break;
#if 0
    case debugDevice_CONSOLE:
        devoptab_list[STD_ERR] = &dotab_stdout;
        break;
    case debugDevice_NULL:
        devoptab_list[STD_ERR] = &dotab_null;
        break;
#endif
    }
    setvbuf(stderr, NULL, buffertype, 0);
}
