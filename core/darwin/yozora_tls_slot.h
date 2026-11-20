#ifndef YOZORA_TLS_SLOT_H
#define YOZORA_TLS_SLOT_H

#include <pthread.h>
#include <cstdlib>

// ios 5 has pthread keys but no native C++ thread-local storage
class YozoraThreadLocalSlot {
public:
    explicit YozoraThreadLocalSlot(void (*destroy)(void*) = nullptr)
    {
        if (pthread_key_create(&m_key, destroy))
            std::abort();
    }
    YozoraThreadLocalSlot(const YozoraThreadLocalSlot&) = delete;
    YozoraThreadLocalSlot& operator=(const YozoraThreadLocalSlot&) = delete;
    void* get() const { return pthread_getspecific(m_key); }
    void set(void* value)
    {
        if (pthread_setspecific(m_key, value))
            std::abort();
    }
private:
    // process-lifetime keys stay valid during worker-thread teardown
    pthread_key_t m_key;
};

#endif
