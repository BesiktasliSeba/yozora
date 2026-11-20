#pragma once

#include "../core/yozora_scene.h"
#include "../yozora_render_plan.h"
#include <skia/core/SkPicture.h>
#include <skia/core/SkRect.h>
#include <cstdint>
#include <memory>
#include <string>
#include <algorithm>
#include <tuple>
#include <chrono>
#include <vector>

namespace Yozora {

inline bool sameLayerState(const yozora_scene_layer& a, const yozora_scene_layer& b)
{
    return std::tie(a.id, a.parent, a.mask, a.x, a.y, a.width, a.height, a.anchor_x, a.anchor_y, a.anchor_z,
               a.bounds_x, a.bounds_y, a.opacity, a.background, a.clips, a.hidden, a.backface_visible,
               a.preserves_3d, a.viewport_clip, a.scroll_id, a.scroll_x, a.scroll_y, a.fixed,
               a.canvas, a.canvas_x, a.canvas_y, a.canvas_width, a.canvas_height)
        == std::tie(b.id, b.parent, b.mask, b.x, b.y, b.width, b.height, b.anchor_x, b.anchor_y, b.anchor_z,
               b.bounds_x, b.bounds_y, b.opacity, b.background, b.clips, b.hidden, b.backface_visible,
               b.preserves_3d, b.viewport_clip, b.scroll_id, b.scroll_x, b.scroll_y, b.fixed,
               b.canvas, b.canvas_x, b.canvas_y, b.canvas_width, b.canvas_height)
        && std::equal(a.transform, a.transform + 16, b.transform)
        && std::equal(a.children_transform, a.children_transform + 16, b.children_transform)
        && a.sticky.scroll_id == b.sticky.scroll_id && a.sticky.edges == b.sticky.edges
        && a.sticky.scroll_x == b.sticky.scroll_x && a.sticky.scroll_y == b.sticky.scroll_y
        && std::equal(a.sticky.viewport, a.sticky.viewport + 4, b.sticky.viewport)
        && std::equal(a.sticky.container, a.sticky.container + 4, b.sticky.container)
        && std::equal(a.sticky.box, a.sticky.box + 4, b.sticky.box)
        && std::equal(a.sticky.offsets, a.sticky.offsets + 4, b.sticky.offsets);
}

struct RecordedLayer {
    struct Animation {
        yozora_scene_animation state { };
        std::vector<yozora_scene_keyframe> frames;
        std::chrono::steady_clock::time_point captured = std::chrono::steady_clock::now();
    };
    struct Change {
        uint64_t version { 0 };
        yozora_damage damage { };
    };
    yozora_scene_layer state { };
    // keeps state.canvas alive from the recording until the raster pass has taken its own reference
    std::shared_ptr<yozora_canvas> canvas;
    uint64_t version { 0 };
    SkRect coverage = SkRect::MakeEmpty();
    sk_sp<SkPicture> picture;
    std::vector<Change> changes;
    std::vector<Animation> animations;
};

struct Scene {
    uint64_t generation { 0 };
    unsigned width { 0 }, height { 0 };
    float scale { 1 };
    int scroll_y { 0 };
    uint64_t scroll_revision { 0 };
    int page_zoom { 100 };
    std::vector<RecordedLayer> layers;
    std::string error;
    std::chrono::steady_clock::time_point captured = std::chrono::steady_clock::now();
};

}

namespace WebCore {
class GraphicsLayer;
class LocalFrameView;
class GraphicsLayerFactory;
GraphicsLayerFactory& yozoraGraphicsLayerFactory();
void yozoraPurgeScene(GraphicsLayer*);
std::shared_ptr<Yozora::Scene> yozoraRecordScene(GraphicsLayer*, LocalFrameView&, unsigned width, unsigned height, float scale, uint64_t generation);
// the id that a layer has in the scene (Yozora::RecordedLayer state.id), 0 for no layer
uint64_t yozoraGraphicsLayerId(GraphicsLayer*);
}
