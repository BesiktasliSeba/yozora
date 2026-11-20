#define _POSIX_C_SOURCE 200809L

#include "yozora_darwin.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char directory[] = "/tmp/yozora-system-XXXXXX";
    char path[256] = { 0 }, original[256] = { 0 }, second[256] = { 0 };
    char unterminated[] = { 'X', 'X', 'X', 'X', 'X', 'X' };
    unsigned char bytes[32];
    struct stat info;
    void *allocation;
    int first, next;
    size_t alignment;

    assert(yozora_darwin_random_bytes(NULL, 0) == 0);
    assert(yozora_darwin_random_bytes(NULL, 1) == EINVAL);
    assert(yozora_darwin_random_bytes(bytes, sizeof(bytes)) == 0);
    for (alignment = sizeof(void *); alignment <= 4096; alignment *= 2) {
        allocation = yozora_darwin_aligned_alloc(alignment, 17);
        assert(allocation && !((uintptr_t)allocation & (alignment - 1)));
        memset(allocation, 0x7f, 17);
        free(allocation);
    }
    errno = 0;
    assert(!yozora_darwin_aligned_alloc(3, 17) && errno == EINVAL);
    assert(yozora_darwin_open_temporary(NULL, 1) == -1 && errno == EINVAL);
    assert(yozora_darwin_open_temporary(unterminated, sizeof(unterminated)) == -1 && errno == EINVAL);
    assert(mkdtemp(directory));
    assert(snprintf(original, sizeof(original), "%s/file-XXXXXX", directory) > 0);
    memcpy(path, original, sizeof(path));
    first = yozora_darwin_open_temporary(path, sizeof(path));
    assert(first >= 0);
    assert(fcntl(first, F_GETFD) & FD_CLOEXEC);
    assert(!fstat(first, &info) && S_ISREG(info.st_mode) && (info.st_mode & 0777) == 0600);
    assert(write(first, "yozora", 5) == 5);
    memcpy(second, original, sizeof(second));
    next = yozora_darwin_open_temporary(second, sizeof(second));
    assert(next >= 0 && strcmp(path, second));
    assert(!close(next));
    assert(!close(first));
    assert(!unlink(path));
    assert(!unlink(second));
    memcpy(path, original, sizeof(path));
    assert(yozora_darwin_open_temporary(path, strlen(path)) == -1 && errno == EINVAL);
    assert(!rmdir(directory));
    memcpy(path, original, sizeof(path));
    assert(yozora_darwin_open_temporary(path, sizeof(path)) == -1 && errno == ENOENT);
    puts("system: aligned allocation, random bounds, exclusive temporary files, permissions and cleanup passed");
    return 0;
}
