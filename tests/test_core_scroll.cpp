#include "config.h"
#include "YozoraCoreInternal.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>

static void publish(yozora_core& core, int version)
{
    pthread_mutex_lock(&core.lock);
    core.shared.scroll_y = version;
    for (unsigned i = 0; i < core.shared.scrollers.size(); ++i) {
        auto& area = core.shared.scrollers[i];
        area.id = 100 + i;
        area.left = version;
        area.top = -version;
        area.pending_x = i;
        area.pending_y = -(int)i;
    }
    pthread_mutex_unlock(&core.lock);
}

static void benchmark(yozora_core& core)
{
    constexpr unsigned passes = 5000, layers = 512;
    for (bool nested : { false, true }) {
        long long sums[2] = { };
        double milliseconds[2] = { };
        for (unsigned batched = 0; batched < 2; ++batched) {
            auto began = std::chrono::steady_clock::now();
            for (unsigned pass = 0; pass < passes; ++pass) {
                yozora_scroll_state state;
                if (batched)
                    yozora_core_read_scroll_state(&core, &state);
                else
                    sums[batched] += yozora_core_scroll_y(&core);
                if (batched)
                    sums[batched] += state.root_y;
                for (unsigned i = 0; i < layers; ++i) {
                    int x, y;
                    uint64_t id = nested && i % 2 ? 100 + i % YOZORA_MAX_SCROLL_AREAS : 1;
                    int found = batched ? yozora_scroll_state_offset(&state, id, &x, &y)
                                        : yozora_core_scroll_offset(&core, id, &x, &y);
                    assert(found);
                    sums[batched] += x + y;
                }
            }
            milliseconds[batched] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        }
        assert(sums[0] == sums[1]);
        std::printf("scroll benchmark: %s, %u passes x %u queries, individual %.3f ms, snapshot %.3f ms, checksum %lld\n",
            nested ? "nested" : "root", passes, layers, milliseconds[0], milliseconds[1], sums[0]);
    }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || (argc == 2 && !std::strcmp(argv[1], "--benchmark")));
    yozora_scroll_state state;
    state.count = 42;
    state.root_y = 42;
    yozora_core_read_scroll_state(nullptr, &state);
    assert(state.root_y == 0 && state.count == 0);
    yozora_core_read_scroll_state(nullptr, nullptr);
    // this fixture exercises the real shared-state boundary without starting WebCore or the network
    yozora_core core;
    yozora_core_read_scroll_state(&core, &state);
    assert(state.count == 0 && state.root_y == 0);
    core.shared.scrollers.resize(YOZORA_MAX_SCROLL_AREAS);
    publish(core, 7);
    yozora_core_read_scroll_state(&core, &state);
    assert(state.root_y == 7 && state.count == YOZORA_MAX_SCROLL_AREAS);
    for (unsigned i = 0; i < state.count; ++i) {
        int x, y;
        assert(yozora_scroll_state_offset(&state, 100 + i, &x, &y));
        assert(x == 7 + (int)i && y == -7 - (int)i);
        int previous_x, previous_y;
        assert(yozora_core_scroll_offset(&core, 100 + i, &previous_x, &previous_y));
        assert(x == previous_x && y == previous_y);
    }
    std::atomic<bool> start { false };
    std::thread writer([&] {
        while (!start.load())
            std::this_thread::yield();
        for (unsigned i = 0; i < 10000; ++i)
            publish(core, i % 2 ? 19 : 37);
    });
    start = true;
    for (unsigned pass = 0; pass < 10000; ++pass) {
        yozora_core_read_scroll_state(&core, &state);
        assert(state.count == YOZORA_MAX_SCROLL_AREAS);
        for (unsigned i = 0; i < state.count; ++i) {
            assert(state.areas[i].id == 100 + i);
            assert(state.areas[i].x == state.root_y + (int)i);
            assert(state.areas[i].y == -state.root_y - (int)i);
        }
    }
    writer.join();
    if (argc == 2)
        benchmark(core);
    core.shared.scrollers.clear();
    yozora_core_read_scroll_state(&core, &state);
    assert(state.count == 0 && state.areas[0].id == 0);
    std::puts("core scroll: empty state, pending movement, 32 areas and 10000 coherent concurrent snapshots passed");
}
