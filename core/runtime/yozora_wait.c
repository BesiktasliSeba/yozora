#define _POSIX_C_SOURCE 200809L
#include "yozora_wait.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define YOZORA_WAIT_BUCKETS 256

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
} wait_bucket;

static wait_bucket buckets[YOZORA_WAIT_BUCKETS];
static pthread_once_t initialize_once = PTHREAD_ONCE_INIT;

static void initialize_buckets(void)
{
    size_t index;
    for (index = 0; index < YOZORA_WAIT_BUCKETS; ++index) {
        int error = pthread_mutex_init(&buckets[index].mutex, NULL);
        if (!error)
            error = pthread_cond_init(&buckets[index].changed, NULL);
        if (error) {
            fprintf(stderr, "yozora: atomic wait initialization failed (%d)\n", error);
            abort();
        }
    }
    /* libc++ atomics may wait during static teardown, the process owns these buckets */
}

static wait_bucket *bucket_for(const void *address)
{
    uintptr_t hash = (uintptr_t)address;
    hash ^= hash >> 11;
    return &buckets[(hash >> 2) & (YOZORA_WAIT_BUCKETS - 1)];
}

static int value_matches(const void *address, const void *previous, size_t size)
{
    typedef uint64_t aligned_u64 __attribute__((aligned(8)));
    if (size == 4) {
        uint32_t old;
        memcpy(&old, previous, sizeof(old));
        return __atomic_load_n((const uint32_t *)address, __ATOMIC_SEQ_CST) == old;
    } else {
        uint64_t old;
        memcpy(&old, previous, sizeof(old));
        return __atomic_load_n((const aligned_u64 *)address, __ATOMIC_SEQ_CST) == old;
    }
}

static int deadline_after(uint64_t timeout_ns, struct timespec *deadline)
{
    struct timeval now;
    uint64_t seconds;
    uint64_t nanoseconds;
    if (gettimeofday(&now, NULL))
        return errno;
    if (now.tv_sec < 0 || now.tv_usec < 0 || now.tv_usec >= 1000000)
        return EINVAL;
    nanoseconds = (uint64_t)now.tv_usec * 1000 + timeout_ns % 1000000000;
    seconds = (uint64_t)now.tv_sec + timeout_ns / 1000000000 + nanoseconds / 1000000000;
    /* ios 5 uses a signed 32-bit time_t */
    if (seconds > (sizeof(time_t) == 4 ? (uint64_t)INT32_MAX : (uint64_t)INT64_MAX))
        return EOVERFLOW;
    deadline->tv_sec = (time_t)seconds;
    deadline->tv_nsec = (long)(nanoseconds % 1000000000);
    return 0;
}

int yozora_wait_address(const void *address, const void *previous, size_t size, uint64_t timeout_ns)
{
    wait_bucket *bucket;
    struct timespec deadline;
    int error;
    int unlock_error;
    if (!address || !previous || (size != 4 && size != 8) || (uintptr_t)address % size)
        return EINVAL;
    error = pthread_once(&initialize_once, initialize_buckets);
    if (error)
        return error;
    if (timeout_ns && (error = deadline_after(timeout_ns, &deadline)) != 0)
        return error;
    bucket = bucket_for(address);
    error = pthread_mutex_lock(&bucket->mutex);
    if (error)
        return error;
    /* checking under the notifier's lock prevents a wake between comparison and waiting */
    if (value_matches(address, previous, size)) {
        if (timeout_ns)
            error = pthread_cond_timedwait(&bucket->changed, &bucket->mutex, &deadline);
        else
            error = pthread_cond_wait(&bucket->changed, &bucket->mutex);
    }
    unlock_error = pthread_mutex_unlock(&bucket->mutex);
    return unlock_error ? unlock_error : error;
}

int yozora_wake_address(const void *address)
{
    wait_bucket *bucket;
    int error;
    int unlock_error;
    if (!address)
        return EINVAL;
    error = pthread_once(&initialize_once, initialize_buckets);
    if (error)
        return error;
    bucket = bucket_for(address);
    error = pthread_mutex_lock(&bucket->mutex);
    if (error)
        return error;
    /* notify_one must not wake only an unrelated waiter in the same bucket */
    error = pthread_cond_broadcast(&bucket->changed);
    unlock_error = pthread_mutex_unlock(&bucket->mutex);
    return unlock_error ? unlock_error : error;
}
