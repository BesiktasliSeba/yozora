#include "yozora_core.h"
#include "yozora_scene.h"

#include <chrono>
#include <cstdio>
#include <thread>

static void collect(yozora_core* core, yozora_scene*& scene)
{
    if (auto* next = yozora_core_take_scene(core)) {
        yozora_scene_release(scene);
        scene = next;
    }
}

int main(int argc, char** argv)
{
    if (argc != 4)
        return 2;
    char error[256] { };
    auto* core = yozora_core_create(argv[1], argv[2], error, sizeof(error));
    if (!core) {
        std::fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (!yozora_core_resize(core, 320, 400, 2) || !yozora_core_load(core, argv[3], error, sizeof(error))) {
        std::fprintf(stderr, "cannot load scene: %s\n", error);
        yozora_core_destroy(core);
        return 1;
    }
    yozora_scene* scene = nullptr;
    for (unsigned i = 0; i < 500 && yozora_core_state(core) == YOZORA_LOAD_LOADING; ++i) {
        collect(core, scene);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (unsigned i = 0; i < 30; ++i) {
        yozora_core_scroll(core, 150);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        collect(core, scene);
    }
    for (unsigned i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        collect(core, scene);
    }
    unsigned red = 0;
    if (scene && !*yozora_scene_error(scene) && yozora_scene_scroll_y(scene) >= 4000) {
        auto* layers = yozora_scene_layers(scene);
        for (unsigned i = 0; i < yozora_scene_layer_count(scene); ++i) {
            for (unsigned j = 0; j < layers[i].tile_count; ++j) {
                const auto& tile = layers[i].tiles[j];
                for (unsigned y = 0; y < tile.height; ++y) {
                    for (unsigned x = 0; x < tile.width; ++x) {
                        auto* p = tile.pixels + y * tile.row_bytes + x * 4;
                        red += p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255;
                    }
                }
            }
        }
    }
    if (!red)
        std::fprintf(stderr, "no red image pixels in the scrolled native scene\n");
    yozora_scene_release(scene);
    yozora_core_destroy(core);
    return red ? 0 : 1;
}
