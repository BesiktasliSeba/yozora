#pragma once

#include "YozoraScene.h"
#include "yozora_core.h"
#include <memory>

namespace Yozora {

// owns a bounded queue and immutable Skia data, no worker operation enters WebCore
class Raster {
public:
    // raster_threads: 0 shares the process painting threads (two on a two core phone, YOZORA_RASTER_THREADS),
    // 1 paints on the raster thread alone, more make an own pool that wide, for tests
    explicit Raster(size_t byte_limit, unsigned raster_threads = 0);
    ~Raster();
    void submit(std::shared_ptr<const Scene>);
    void scroll(int y, uint64_t revision = 0);
    void reset();
    // frees the tiles that the last pass did not use and keeps what it made, for a first warning about memory
    void trimUnused();
    bool pending() const;
    std::string error() const;
    yozora_scene* takeScene();
    bool takeFrame(yozora_frame&);
    void statistics(yozora_core_stats&) const;
private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
