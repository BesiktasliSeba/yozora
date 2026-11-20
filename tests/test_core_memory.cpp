#include "config.h"
#include "yozora_core.h"
#include "YozoraCoreThread.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

int main(int argc, char** argv)
{
    assert(argc == 3);
    yozora_initialize_javascript();
    char error[256];
    auto* current = yozora_core_create(argv[1], argv[2], error, sizeof(error));
    auto* retired = yozora_core_create(argv[1], argv[2], error, sizeof(error));
    assert(current && retired);
    std::mutex lock;
    std::condition_variable changed;
    bool entered = false, resume = false, returned = false;
    YozoraCoreThread::post([&] {
        std::unique_lock guard(lock);
        entered = true;
        changed.notify_all();
        changed.wait(guard, [&] { return resume; });
    });
    {
        std::unique_lock guard(lock);
        changed.wait(guard, [&] { return entered; });
    }
    std::thread interface([&] {
        for (unsigned i = 0; i < 64; ++i)
            yozora_release_memory_async();
        yozora_core_destroy_async(retired);
        std::lock_guard guard(lock);
        returned = true;
        changed.notify_all();
    });
    bool responsive;
    {
        std::unique_lock guard(lock);
        responsive = changed.wait_for(guard, std::chrono::seconds(1), [&] { return returned; });
        // release the gate even when a synchronous regression would otherwise hang the test
        resume = true;
        changed.notify_all();
    }
    interface.join();
    YozoraCoreThread::call([] { });
    assert(responsive);
    // a later pressure event must run again after the first batch has finished
    yozora_release_memory_async();
    YozoraCoreThread::call([] { });
    assert(yozora_core_resize(current, 320, 400, 2));
    yozora_core_destroy_async(current);
    YozoraCoreThread::call([] { });
    yozora_core_destroy_async(nullptr);
    std::puts("core memory: pressure and retirement return while the core is blocked, queued cleanup and later pressure complete");
}
