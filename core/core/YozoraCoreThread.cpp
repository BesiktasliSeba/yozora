#include "config.h"
#include "YozoraCoreThread.h"
#include "yozora_render_plan.h"

#include <pthread.h>
#include <time.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <wtf/Deque.h>
#include <wtf/RunLoop.h>
#include <wtf/MonotonicTime.h>
#include <wtf/Seconds.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

// plain pthread primitives: the user interface thread uses them before WTF knows about it
namespace YozoraCoreThread {

namespace {

constexpr int64_t tickIntervalUs = 16667;
constexpr int64_t idleIntervalUs = 100000;
constexpr size_t stackBytes = 8 * 1024 * 1024;

struct State {
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t wakeup = PTHREAD_COND_INITIALIZER;
    pthread_cond_t finished = PTHREAD_COND_INITIALIZER;
    pthread_t thread;
    bool started { false };
    bool paused { false };
    // the app is hidden but a transfer runs: the run loop serves it and no pass renders a page
    bool quiet { false };
    bool (*tick)() { nullptr };
    WTF::Deque<WTF::Function<void()>> queue;
    unsigned long long completed { 0 };
    unsigned long long enqueued { 0 };
    // set by the core thread once WTF has a main run loop, read by the other threads under the lock
    RefPtr<WTF::RunLoop> loop;
    // core thread only
    std::unique_ptr<WTF::RunLoop::Timer> tickTimer;
    std::atomic<bool> tickQueued { false };
    int64_t nextTickUs { 0 };
    int64_t scheduledTickUs { 0 };
};

// never destroyed: the thread keeps using it while the process exits
State& state()
{
    static State* instance = new State;
    return *instance;
}

// true when it ran something
bool runQueued()
{
    auto& s = state();
    bool ran = false;
    for (;;) {
        WTF::Deque<WTF::Function<void()>> work;
        pthread_mutex_lock(&s.lock);
        std::swap(work, s.queue);
        pthread_mutex_unlock(&s.lock);
        if (work.isEmpty())
            return ran;
        ran = true;
        while (!work.isEmpty()) {
            auto item = work.takeFirst();
            item();
            pthread_mutex_lock(&s.lock);
            ++s.completed;
            pthread_cond_broadcast(&s.finished);
            pthread_mutex_unlock(&s.lock);
        }
    }
}

// a paused thread holds still inside the run loop, so page timers and network callbacks wait too, and it
// keeps running queued work
void pauseBarrier()
{
    auto& s = state();
    for (;;) {
        pthread_mutex_lock(&s.lock);
        bool paused = s.paused;
        if (paused && s.queue.isEmpty())
            pthread_cond_wait(&s.wakeup, &s.lock);
        pthread_mutex_unlock(&s.lock);
        if (!paused)
            return;
        runQueued();
    }
}

bool runTick()
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    bool paused = s.paused;
    bool quiet = s.quiet;
    auto tick = s.tick;
    pthread_mutex_unlock(&s.lock);
    return !paused && !quiet && tick && tick();
}

static int64_t monotonicMicroseconds()
{
    return static_cast<int64_t>(WTF::MonotonicTime::now().secondsSinceEpoch().microseconds());
}

void scheduleTick(int64_t deadline)
{
    auto& s = state();
    // repeated invalidations share the first deadline instead of postponing the frame
    if (!s.tickTimer || (s.scheduledTickUs && s.scheduledTickUs <= deadline))
        return;
    s.scheduledTickUs = deadline;
    auto remaining = std::max<int64_t>(0, deadline - monotonicMicroseconds());
    s.tickTimer->startOneShot(WTF::Seconds::fromMicroseconds(remaining));
}

void requestTick()
{
    scheduleTick(state().nextTickUs);
}

// keep frame deadlines anchored when a pass runs long, quiet pages only publish state ten times a second
void tickAndReschedule()
{
    auto& s = state();
    s.scheduledTickUs = 0;
    s.nextTickUs = yozora_frame_deadline(monotonicMicroseconds(), s.nextTickUs, tickIntervalUs);
    bool busy = runTick();
    auto now = monotonicMicroseconds();
    s.nextTickUs = yozora_frame_deadline(now, s.nextTickUs, tickIntervalUs);
    scheduleTick(busy ? s.nextTickUs : now + idleIntervalUs);
}

// work queued from another thread runs as a run loop task, which sleeps until a task or timer is due, and a
// pass follows so what the work changed reaches the interface at once
void dispatchQueued()
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    auto loop = s.loop;
    pthread_cond_signal(&s.wakeup);
    pthread_mutex_unlock(&s.lock);
    if (loop) {
        loop->dispatch([] {
            if (runQueued())
                requestTick();
        });
    }
}

void* thread_main(void*)
{
    auto& s = state();
    // the first queued pass creates the first core, which starts WTF on this thread and attaches its run
    // loop, until then the queue has a wait of its own
    for (;;) {
        runQueued();
        pthread_mutex_lock(&s.lock);
        while (s.queue.isEmpty() && !s.loop)
            pthread_cond_wait(&s.wakeup, &s.lock);
        bool attached = s.loop;
        pthread_mutex_unlock(&s.lock);
        if (attached)
            break;
    }
    runQueued();
    WTF::RunLoop::run();
    return nullptr;
}

// called with the lock held, starts the thread on first use
void ensure_started(State& s)
{
    if (s.started)
        return;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, stackBytes);
    int result = pthread_create(&s.thread, &attributes, thread_main, nullptr);
    pthread_attr_destroy(&attributes);
    if (result) {
        std::fprintf(stderr, "yozora: core thread did not start: %d\n", result);
        std::abort();
    }
    s.started = true;
}

}

void attachRunLoop()
{
    auto& s = state();
    // WTF decided on this thread that it is the main thread, the run loop it owns is the one every page uses
    Ref loop = WTF::RunLoop::currentSingleton();
    s.tickTimer = std::make_unique<WTF::RunLoop::Timer>(loop.copyRef(), "yozora tick"_s, [] { tickAndReschedule(); });
    pthread_mutex_lock(&s.lock);
    s.loop = WTF::move(loop);
    bool paused = s.paused;
    pthread_cond_signal(&s.wakeup);
    pthread_mutex_unlock(&s.lock);
    requestTick();
    // the app went to the background before the first core existed
    if (paused)
        WTF::RunLoop::currentSingleton().dispatch([] { pauseBarrier(); });
}

void post(WTF::Function<void()>&& work)
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    ensure_started(s);
    s.queue.append(WTF::move(work));
    ++s.enqueued;
    pthread_mutex_unlock(&s.lock);
    dispatchQueued();
}

void call(WTF::Function<void()>&& work)
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    ensure_started(s);
    s.queue.append(WTF::move(work));
    unsigned long long ticket = ++s.enqueued;
    pthread_mutex_unlock(&s.lock);
    dispatchQueued();
    pthread_mutex_lock(&s.lock);
    // completions are counted in order, so this one is done when the counter reaches its ticket
    while (s.completed < ticket)
        pthread_cond_wait(&s.finished, &s.lock);
    pthread_mutex_unlock(&s.lock);
}

void wake()
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    auto loop = s.loop;
    pthread_mutex_unlock(&s.lock);
    // a request that is already waiting for its turn covers this one
    if (loop && !s.tickQueued.exchange(true)) {
        loop->dispatch([] {
            state().tickQueued = false;
            requestTick();
        });
    }
}

void setTick(bool (*tick)())
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    s.tick = tick;
    pthread_mutex_unlock(&s.lock);
}

void setPaused(bool paused)
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    s.paused = paused;
    auto loop = s.loop;
    pthread_cond_signal(&s.wakeup);
    pthread_mutex_unlock(&s.lock);
    if (paused && loop)
        loop->dispatch([] { pauseBarrier(); });
}

void setQuiet(bool quiet)
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    s.quiet = quiet;
    pthread_mutex_unlock(&s.lock);
}

bool isPaused()
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    bool result = s.paused;
    pthread_mutex_unlock(&s.lock);
    return result;
}

bool isCurrent()
{
    auto& s = state();
    pthread_mutex_lock(&s.lock);
    bool result = s.started && pthread_equal(pthread_self(), s.thread);
    pthread_mutex_unlock(&s.lock);
    return result;
}

long long cpuMilliseconds()
{
#if defined(__APPLE__)
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    mach_port_t port = mach_thread_self();
    kern_return_t result = thread_info(port, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &count);
    mach_port_deallocate(mach_task_self(), port);
    if (result != KERN_SUCCESS)
        return 0;
    return (static_cast<long long>(info.user_time.seconds) + info.system_time.seconds) * 1000
        + (static_cast<long long>(info.user_time.microseconds) + info.system_time.microseconds) / 1000;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now))
        return 0;
    return static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
#endif
}

}
