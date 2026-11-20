#include "yozora_wait.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned ready;
    /* armv7 gives uint64_t four-byte alignment, atomic waits require eight */
    uint64_t value __attribute__((aligned(8)));
} wait_fixture;

static void *waiter(void *context)
{
    wait_fixture *fixture = context;
    uint64_t old = 0;
    unsigned attempt;
    assert(!pthread_mutex_lock(&fixture->mutex));
    ++fixture->ready;
    assert(!pthread_cond_signal(&fixture->changed));
    assert(!pthread_mutex_unlock(&fixture->mutex));
    for (attempt = 0; attempt < 10; ++attempt) {
        int error;
        if (__atomic_load_n(&fixture->value, __ATOMIC_SEQ_CST) != old)
            return NULL;
        error = yozora_wait_address(&fixture->value, &old, sizeof(old), 500000000);
        assert(error == 0 || error == ETIMEDOUT);
    }
    assert(!"atomic notify did not make progress");
    return NULL;
}

int main(void)
{
    uint32_t small = 1, previous = 0;
    uint64_t large __attribute__((aligned(8))) = 0;
    pthread_t threads[4];
    wait_fixture fixture = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 };
    unsigned index;
    assert(yozora_wait_address(NULL, &previous, 4, 0) == EINVAL);
    assert(yozora_wait_address(&small, NULL, 4, 0) == EINVAL);
    assert(yozora_wait_address(&small, &previous, 3, 0) == EINVAL);
    assert(yozora_wait_address((char *)&large + 1, &large, 8, 0) == EINVAL);
    assert(yozora_wake_address(NULL) == EINVAL);
    assert(!yozora_wait_address(&small, &previous, 4, 0));
    previous = 1;
    assert(yozora_wait_address(&small, &previous, 4, 1000000) == ETIMEDOUT);
    for (index = 0; index < 4; ++index)
        assert(!pthread_create(&threads[index], NULL, waiter, &fixture));
    assert(!pthread_mutex_lock(&fixture.mutex));
    while (fixture.ready != 4)
        assert(!pthread_cond_wait(&fixture.changed, &fixture.mutex));
    assert(!pthread_mutex_unlock(&fixture.mutex));
    __atomic_store_n(&fixture.value, 1, __ATOMIC_SEQ_CST);
    assert(!yozora_wake_address(&fixture.value));
    for (index = 0; index < 4; ++index)
        assert(!pthread_join(threads[index], NULL));
    assert(!pthread_cond_destroy(&fixture.changed));
    assert(!pthread_mutex_destroy(&fixture.mutex));
    puts("wait: checked inputs, changed value, timeout, notify and concurrent waiters passed");
    return 0;
}
