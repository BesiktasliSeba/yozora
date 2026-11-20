#if !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "yozora_darwin.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int yozora_darwin_random_bytes(void *buffer, size_t capacity)
{
    if (!buffer && capacity)
        return EINVAL;
    if (capacity)
        arc4random_buf(buffer, capacity);
    return 0;
}

void *yozora_darwin_aligned_alloc(size_t alignment, size_t size)
{
    void *memory = NULL;
    int error;
    if (alignment < sizeof(void *) || (alignment & (alignment - 1))) {
        errno = EINVAL;
        return NULL;
    }
    /* ios 5 has posix_memalign, aligned_alloc requires ios 11 */
    error = posix_memalign(&memory, alignment, size);
    if (error) {
        errno = error;
        return NULL;
    }
    return memory;
}

int yozora_darwin_open_temporary(char *path, size_t capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char *end;
    size_t length, i;
    unsigned attempt;
    if (!path || !capacity || !(end = (char *)memchr(path, '\0', capacity))) {
        errno = EINVAL;
        return -1;
    }
    length = (size_t)(end - path);
    if (length < 6 || memcmp(end - 6, "XXXXXX", 6)) {
        errno = EINVAL;
        return -1;
    }
    /* ios 5 lacks mkostemp, open sets close-on-exec before publishing the descriptor */
    for (attempt = 0; attempt < 128; ++attempt) {
        unsigned char bytes[6];
        int descriptor;
        arc4random_buf(bytes, sizeof(bytes));
        for (i = 0; i < sizeof(bytes); ++i)
            (end - 6)[i] = alphabet[bytes[i] & 63];
        descriptor = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (descriptor >= 0 || errno != EEXIST)
            return descriptor;
    }
    errno = EEXIST;
    return -1;
}
