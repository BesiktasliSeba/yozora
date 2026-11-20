#pragma once

#include <atomic>
#include <cstdio>

// a statistical profiler for the render tool: a thread suspends every running thread each few hundred
// microseconds and writes the program counter and frame pointer chain. itimer signals tick at 100 Hz on ios
// and arrive on any thread, too coarse for a 5 second page load
#if defined(__APPLE__) && (defined(__arm__) || defined(__aarch64__))

#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace YozoraSampler {

constexpr unsigned maxDepth = 160;
constexpr unsigned maxSamples = 36000;

struct Sample {
    uint32_t thread;
    uint32_t depth;
    uintptr_t frames[maxDepth];
};

static Sample samples[maxSamples];
static unsigned count = 0;
static std::atomic<bool> running { false };
static unsigned interval_us = 1000;
static unsigned depth_limit = maxDepth;
static pthread_t sampler;
static mach_port_t sampler_port = MACH_PORT_NULL;

#if defined(__arm__)
typedef arm_thread_state_t State;
static const thread_state_flavor_t flavor = ARM_THREAD_STATE;
static const mach_msg_type_number_t state_count = ARM_THREAD_STATE_COUNT;
static uintptr_t state_pc(const State& s) { return s.__pc; }
static uintptr_t state_lr(const State& s) { return s.__lr; }
static uintptr_t state_sp(const State& s) { return s.__sp; }
// thumb code keeps the frame pointer in r7
static uintptr_t state_fp(const State& s) { return s.__r[7]; }
#else
typedef arm_thread_state64_t State;
static const thread_state_flavor_t flavor = ARM_THREAD_STATE64;
static const mach_msg_type_number_t state_count = ARM_THREAD_STATE64_COUNT;
static uintptr_t state_pc(const State& s) { return s.__pc; }
static uintptr_t state_lr(const State& s) { return s.__lr; }
static uintptr_t state_sp(const State& s) { return s.__sp; }
static uintptr_t state_fp(const State& s) { return s.__fp; }
#endif

// the thread is suspended, so its stack does not change while the chain is read
static void walk(Sample& sample, const State& state, uintptr_t stack_top)
{
    sample.frames[sample.depth++] = state_pc(state);
    if (depth_limit > 1)
        sample.frames[sample.depth++] = state_lr(state);
    uintptr_t sp = state_sp(state);
    uintptr_t fp = state_fp(state);
    while (sample.depth < depth_limit) {
        if (fp < sp || fp + 2 * sizeof(uintptr_t) > stack_top || (fp & (sizeof(uintptr_t) - 1)))
            break;
        const uintptr_t* frame = reinterpret_cast<const uintptr_t*>(fp);
        uintptr_t next = frame[0];
        sample.frames[sample.depth++] = frame[1];
        if (next <= fp)
            break;
        fp = next;
    }
}

static void* loop(void*)
{
    sampler_port = mach_thread_self();
    while (running) {
        usleep(interval_us);
        thread_act_array_t threads;
        mach_msg_type_number_t thread_count = 0;
        if (task_threads(mach_task_self(), &threads, &thread_count) != KERN_SUCCESS)
            continue;
        for (mach_msg_type_number_t i = 0; i < thread_count; ++i) {
            mach_port_t thread = threads[i];
            if (thread == sampler_port || count >= maxSamples)
                continue;
            thread_basic_info_data_t info;
            mach_msg_type_number_t info_count = THREAD_BASIC_INFO_COUNT;
            if (thread_info(thread, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &info_count) != KERN_SUCCESS || info.run_state != TH_STATE_RUNNING)
                continue;
            if (thread_suspend(thread) != KERN_SUCCESS)
                continue;
            State state;
            mach_msg_type_number_t size = state_count;
            if (thread_get_state(thread, flavor, reinterpret_cast<thread_state_t>(&state), &size) == KERN_SUCCESS) {
                pthread_t owner = pthread_from_mach_thread_np(thread);
                uintptr_t top = owner ? reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(owner)) : 0;
                Sample& sample = samples[count];
                sample.thread = static_cast<uint32_t>(thread);
                sample.depth = 0;
                if (top > state_sp(state))
                    walk(sample, state, top);
                else {
                    sample.frames[sample.depth++] = state_pc(state);
                    sample.frames[sample.depth++] = state_lr(state);
                }
                ++count;
            }
            thread_resume(thread);
        }
        for (mach_msg_type_number_t i = 0; i < thread_count; ++i)
            mach_port_deallocate(mach_task_self(), threads[i]);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), thread_count * sizeof(thread_t));
    }
    return nullptr;
}

static bool start(unsigned microseconds, unsigned depth)
{
    interval_us = microseconds;
    depth_limit = depth < maxDepth ? depth : maxDepth;
    running = true;
    int result = pthread_create(&sampler, nullptr, loop, nullptr);
    if (result) {
        running = false;
        std::fprintf(stderr, "yozora-render: sampler thread did not start: %d\n", result);
        return false;
    }
    return true;
}

// one line for the image, then one per sample: thread, then the addresses from the leaf outward
static bool stop(const char* path)
{
    running = false;
    pthread_join(sampler, nullptr);
    FILE* output = std::fopen(path, "w");
    if (!output) {
        std::fprintf(stderr, "yozora-render: profile %s: %s\n", path, std::strerror(errno));
        return false;
    }
    std::fprintf(output, "I %lx %u\n", static_cast<unsigned long>(_dyld_get_image_vmaddr_slide(0)), interval_us);
    for (unsigned i = 0; i < count; ++i) {
        std::fprintf(output, "S %x", samples[i].thread);
        for (unsigned frame = 0; frame < samples[i].depth; ++frame)
            std::fprintf(output, " %lx", static_cast<unsigned long>(samples[i].frames[frame]));
        std::fputc('\n', output);
    }
    bool written = !std::ferror(output);
    if (std::fclose(output))
        written = false;
    if (!written)
        std::fprintf(stderr, "yozora-render: profile %s was not written\n", path);
    else
        std::fprintf(stderr, "yozora-render: profile %u samples in %s\n", count, path);
    return written;
}

}

#else

namespace YozoraSampler {

static bool start(unsigned, unsigned) { std::fputs("yozora-render: the profiler needs an arm ios build\n", stderr); return false; }
static bool stop(const char*) { return false; }

}

#endif
