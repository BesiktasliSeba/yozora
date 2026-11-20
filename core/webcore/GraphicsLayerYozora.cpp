#include "config.h"
#include "GraphicsLayer.h"
#include "YozoraScene.h"

#include "GraphicsContextSkia.h"
#include "GraphicsLayerFactory.h"
#include "GraphicsLayerAnimation.h"
#include "GraphicsLayerFloatAnimationValue.h"
#include "GraphicsLayerTransformAnimationValue.h"
#include "GraphicsLayerKeyframeValueList.h"
#include "AnimationUtilities.h"
#include "TransformOperations.h"
#include "TimingFunction.h"
#include <wtf/MonotonicTime.h>
#include <cmath>
#include "LocalFrameView.h"
#include "RenderLayer.h"
#include "RenderBox.h"
#include "RenderBoxModelObject.h"
#include "RenderLayerScrollableArea.h"
#include "ScrollingConstraints.h"
#include "RenderLayerBacking.h"
#include "RenderLayerCompositor.h"
#include "RenderLayerModelObject.h"
#include "RenderView.h"
#include "RenderObjectInlines.h"
#include "RenderElementInlines.h"
#include "../yozora_render_plan.h"
#include "../yozora_log.h"
#include <skia/core/SkBBHFactory.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkPictureRecorder.h>
#include <algorithm>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <map>
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>
#if ENABLE(WEBGL)
#include "GraphicsContextGLYozora.h"
#endif

namespace WebCore {

class YozoraGraphicsLayer final : public GraphicsLayer {
    WTF_MAKE_TZONE_ALLOCATED(YozoraGraphicsLayer);
public:
    YozoraGraphicsLayer(Type type, GraphicsLayerClient& client)
        : GraphicsLayer(type, client)
        , m_id(++nextID)
    {
        m_damage.full = 1;
    }
    ~YozoraGraphicsLayer() final
    {
#if ENABLE(WEBGL)
        // the context outlives its layer, and must not call into it
        if (m_canvasDelegate)
            m_canvasDelegate->setObserver(nullptr);
#endif
        willBeDestroyed();
    }

    uint64_t id() const { return m_id; }
    void purge()
    {
        m_picture.reset();
        m_changes.clear();
        m_damage.full = 1;
    }
    void setNeedsDisplay() final
    {
        m_damage.full = 1;
        client().notifyFlushRequired(this);
    }
    void setNeedsDisplayInRect(const FloatRect& rect, ShouldClipToLayer) final
    {
        auto rounded = enclosingIntRect(rect);
        yozora_damage_add(&m_damage, { rounded.x(), rounded.y(), rounded.width(), rounded.height() });
        client().notifyFlushRequired(this);
    }
    void setContentsNeedsDisplay() final { setNeedsDisplay(); }
    bool shouldRepaintOnSizeChange() const final { return drawsContent() || m_solid.isValid() || hasCanvasFrame(); }
    bool shouldDirectlyCompositeImage(Image*) const final { return false; }
    bool shouldDirectlyCompositeImageBuffer(ImageBuffer*) const final { return false; }
    bool setFilters(const FilterOperations& filters) final { return filters.isEmpty(); }
    bool setBackdropFilters(const FilterOperations& filters) final { return filters.isEmpty(); }
#if ENABLE(WEBGL)
    // a WebGL canvas: the layer shows the native layer the context presents into, or draws the last picture the
    // context read back, inside the contents rect
    void setContentsDisplayDelegate(RefPtr<GraphicsLayerContentsDisplayDelegate>&& delegate, ContentsLayerPurpose) final
    {
        if (m_canvasDelegate)
            m_canvasDelegate->setObserver(nullptr);
        m_canvasDelegate = delegate && delegate->platformLayer() == YozoraGLDisplayDelegate::tag() ? static_cast<YozoraGLDisplayDelegate*>(delegate.get()) : nullptr;
        if (m_canvasDelegate)
            m_canvasDelegate->setObserver([this] { setNeedsDisplay(); });
        setNeedsDisplay();
    }
#endif
    // the native canvas layer is placed by this rect, and nothing else tells the scene when it moves
    void setContentsRect(const FloatRect& rect) final
    {
        if (contentsRect() == rect)
            return;
        GraphicsLayer::setContentsRect(rect);
        if (hasCanvasSurface())
            client().notifyFlushRequired(this);
    }
    void setContentsToSolidColor(const Color& color) final
    {
        m_solid = color;
        setNeedsDisplay();
    }
    void setPosition(const FloatPoint& value) final
    {
        if (position() == value)
            return;
        GraphicsLayer::setPosition(value);
        client().notifyFlushRequired(this);
    }
    void setTransform(const TransformationMatrix& value) final
    {
        GraphicsLayer::setTransform(value);
        client().notifyFlushRequired(this);
    }
    void setOpacity(float value) final
    {
        if (opacity() == value)
            return;
        GraphicsLayer::setOpacity(value);
        client().notifyFlushRequired(this);
    }
    bool setChildren(Vector<Ref<GraphicsLayer>>&& value) final
    {
        bool changed = GraphicsLayer::setChildren(WTF::move(value));
        if (changed)
            client().notifyFlushRequired(this);
        return changed;
    }

    bool addAnimation(const GraphicsLayerKeyframeValueList& values, const GraphicsLayerAnimation* animation, const String& name, double offset) final
    {
        if (!animation || !animation->duration() || !std::isfinite(*animation->duration()) || *animation->duration() < 0.0001
            || values.size() < 2 || values.size() > 256 || !std::isfinite(offset)
            || (animation->playState() == GraphicsLayerAnimation::PlayState::Paused && offset < 0)
            || animation->compositeOperation() != CompositeOperation::Replace || animation->playbackRate() != 1
            || (animation->direction() != GraphicsLayerAnimation::Direction::Normal && animation->direction() != GraphicsLayerAnimation::Direction::Alternate)
            || std::isnan(animation->iterationCount())
            || (animation->iterationCount() < 0 && animation->iterationCount() != GraphicsLayerAnimation::IterationCountInfinite))
            return false;
        bool transform = values.property() == AnimatedProperty::Transform;
        if (!transform && values.property() != AnimatedProperty::Opacity)
            return false;
        // a transform recording retains the whole layer so motion can expose it without WebCore. a layer the
        // size of the screen (a drawer, a carousel, a sliding page) is the usual case, so the bound is only
        // for a layer no phone could hold
        if (transform && (size().width() > 16384 || size().height() > 16384 || size().area() > 16777216))
            return false;
        RunningAnimation running;
        auto& state = running.value.state;
        running.name = name;
        running.begin = MonotonicTime::now() - Seconds(offset);
        state.id = ++nextID;
        state.property = transform ? 2 : 1;
        state.duration = *animation->duration();
        // web animations supplies positive infinity, the scene protocol uses -1
        state.iterations = std::isinf(animation->iterationCount()) ? -1 : animation->iterationCount();
        state.alternate = animation->direction() == GraphicsLayerAnimation::Direction::Alternate;
        state.fill = static_cast<int>(animation->fillMode());
        state.paused = animation->playState() == GraphicsLayerAnimation::PlayState::Paused;
        state.elapsed = offset;
        running.explicitly_paused = state.paused;
        // the keyframes of a transform that is more than a translation, kept to be sampled below
        struct TransformKeyframe {
            double time;
            const TransformOperations* operations;
            RefPtr<const TimingFunction> timing;
        };
        std::vector<TransformKeyframe> transformKeyframes;
        bool sampled = false;
        for (size_t i = 0; i < values.size(); ++i) {
            const auto& value = values.at(i);
            yozora_scene_keyframe frame { };
            frame.time = value.keyTime();
            if (!std::isfinite(frame.time) || frame.time < 0 || frame.time > 1
                || (i && frame.time < running.value.frames.back().time))
                return false;
            auto* timing = value.timingFunction();
            RefPtr defaultTiming = animation->defaultTimingFunctionForKeyframes();
            if (!timing)
                timing = defaultTiming.get();
            if (!timing) {
                defaultTiming = animation->timingFunction();
                timing = defaultTiming.get();
            }
            frame.bezier[2] = frame.bezier[3] = 1;
            if (timing) {
                if (auto* cubic = dynamicDowncast<CubicBezierTimingFunction>(timing)) {
                    frame.bezier[0] = cubic->x1(); frame.bezier[1] = cubic->y1();
                    frame.bezier[2] = cubic->x2(); frame.bezier[3] = cubic->y2();
                    if (!std::all_of(frame.bezier, frame.bezier + 4, [](float value) { return std::isfinite(value); }))
                        return false;
                } else if (!timing->isLinearTimingFunction() || !downcast<LinearTimingFunction>(*timing).points().isEmpty())
                    return false;
            }
            if (transform) {
                auto* transformed = dynamicDowncast<GraphicsLayerTransformAnimationValue>(value);
                if (!transformed)
                    return false;
                TransformationMatrix matrix;
                transformed->value().apply(matrix);
                sampled = sampled || !matrix.isIdentityOrTranslation();
                transformKeyframes.push_back({ frame.time, &transformed->value(), timing });
                auto values = matrix.toColumnMajorFloatArray();
                for (unsigned j = 0; j < 16; ++j) {
                    frame.values[j] = values[j];
                    if (!std::isfinite(frame.values[j]))
                        return false;
                }
            } else {
                auto* scalar = dynamicDowncast<GraphicsLayerFloatAnimationValue>(value);
                if (!scalar || !std::isfinite(scalar->value()))
                    return false;
                frame.values[0] = scalar->value();
            }
            running.value.frames.push_back(frame);
        }
        if (running.value.frames.front().time != 0 || running.value.frames.back().time != 1)
            return false;
        // a rotation, scale or skew cannot go between two matrices element by element (360 degrees begins and
        // ends at one matrix, a quarter turn passes through a smaller picture), so the motion is sampled:
        // WebCore blends the transform lists with each keyframe's easing, the sample matrices are recorded
        // and the compositor goes from sample to sample
        if (sampled) {
            size_t segments = transformKeyframes.size() - 1;
            if (segments > YOZORA_ANIMATION_MAX_FRAMES / 2)
                return false;
            unsigned steps = std::clamp<unsigned>(static_cast<unsigned>((YOZORA_ANIMATION_MAX_FRAMES - 1) / segments), 2, 32);
            std::vector<yozora_scene_keyframe> samples;
            samples.reserve(segments * steps + 1);
            auto sample = [&](double time, const TransformOperations& operations) {
                TransformationMatrix matrix;
                operations.apply(matrix);
                yozora_scene_keyframe frame { };
                frame.time = time;
                auto matrixValues = matrix.toColumnMajorFloatArray();
                for (unsigned j = 0; j < 16; ++j) {
                    frame.values[j] = matrixValues[j];
                    if (!std::isfinite(frame.values[j]))
                        return false;
                }
                // the easing is inside the samples, between two of them the motion is straight
                frame.bezier[2] = frame.bezier[3] = 1;
                samples.push_back(frame);
                return true;
            };
            for (size_t i = 0; i < segments; ++i) {
                auto& from = transformKeyframes[i];
                auto& to = transformKeyframes[i + 1];
                double span = to.time - from.time;
                for (unsigned step = 0; step < steps; ++step) {
                    double progress = static_cast<double>(step) / steps;
                    double eased = from.timing ? from.timing->transformProgress(progress, *animation->duration() * span) : progress;
                    if (!sample(from.time + progress * span, blend(*from.operations, *to.operations, BlendingContext(eased))))
                        return false;
                }
            }
            if (!sample(1, *transformKeyframes.back().operations))
                return false;
            running.value.frames = WTF::move(samples);
        }
        removeAnimation(name, values.property());
        m_animations.push_back(WTF::move(running));
        client().notifyAnimationStarted(this, name, MonotonicTime::now());
        client().notifyFlushRequired(this);
        return true;
    }
    void pauseAnimation(const String& name, double offset) final
    {
        for (auto& animation : m_animations) {
            if (animation.name == name) {
                animation.value.state.elapsed = offset;
                animation.value.state.paused = 1;
                animation.value.state.id = ++nextID;
                animation.explicitly_paused = true;
            }
        }
        client().notifyFlushRequired(this);
    }
    void removeAnimation(const String& name, std::optional<AnimatedProperty> property) final
    {
        std::erase_if(m_animations, [&](auto& animation) {
            return animation.name == name && (!property || (*property == AnimatedProperty::Opacity ? 1u : 2u) == animation.value.state.property);
        });
        client().notifyFlushRequired(this);
    }
    void suspendAnimations(MonotonicTime now) final
    {
        for (auto& animation : m_animations) {
            if (!animation.value.state.paused) {
                animation.value.state.elapsed = (now - animation.begin).seconds();
                animation.value.state.paused = 1;
                animation.value.state.id = ++nextID;
            }
        }
        client().notifyFlushRequired(this);
    }
    void resumeAnimations() final
    {
        for (auto& animation : m_animations) {
            if (animation.value.state.paused && !animation.explicitly_paused) {
                animation.begin = MonotonicTime::now() - Seconds(animation.value.state.elapsed);
                animation.value.state.paused = 0;
                animation.value.state.id = ++nextID;
            }
        }
        client().notifyFlushRequired(this);
    }

    Yozora::RecordedLayer record(const FloatRect& visible)
    {
        Yozora::RecordedLayer result;
        auto& state = result.state;
        state.id = m_id;
        state.parent = parent() ? static_cast<YozoraGraphicsLayer*>(parent())->id() : 0;
        state.mask = maskLayer() ? static_cast<YozoraGraphicsLayer*>(maskLayer())->id() : 0;
        state.x = position().x();
        state.y = position().y();
        state.width = size().width();
        state.height = size().height();
        state.anchor_x = anchorPoint().x();
        state.anchor_y = anchorPoint().y();
        state.anchor_z = anchorPoint().z();
        state.bounds_x = boundsOrigin().x();
        state.bounds_y = boundsOrigin().y();
        auto transform_values = transform().toColumnMajorFloatArray();
        auto child_values = childrenTransform().toColumnMajorFloatArray();
        for (unsigned i = 0; i < 16; ++i) {
            state.transform[i] = transform_values[i];
            state.children_transform[i] = child_values[i];
        }
        state.opacity = opacity();
        state.background = backgroundColor().isValid() ? static_cast<SkColor>(backgroundColor()) : 0;
        state.clips = masksToBounds();
        state.hidden = !contentsAreVisible();
        state.backface_visible = backfaceVisibility();
        state.preserves_3d = preserves3D();
#if ENABLE(WEBGL)
        if (hasCanvasSurface()) {
            // the rect is empty until the backing sets one for contents that are not clipped: the surface then fills the layer
            auto rect = contentsRect().isEmpty() ? FloatRect(FloatPoint(), size()) : contentsRect();
            auto* surface = m_canvasDelegate->surface();
            surface->retain(surface);
            result.canvas = std::shared_ptr<yozora_canvas>(surface, [](yozora_canvas* canvas) { canvas->release(canvas); });
            state.canvas = surface;
            state.canvas_x = rect.x();
            state.canvas_y = rect.y();
            state.canvas_width = rect.width();
            state.canvas_height = rect.height();
        }
#endif
        // a layer whose bounds start away from zero (a block scrolled inside itself) can be read two ways: its
        // contents lie over [0, size] or over [origin, origin + size]. the layer matrix subtracts the origin, which
        // says the second, but the first is what this recorded until now, and a record of the second alone made a
        // captcha vanish on a phone. both are kept: the region is the box around the two, a bigger picture and the
        // raster takes what it needs of it. the `layer` log lines say which reading the page needs
        FloatRect zeroBounds(FloatPoint(), size());
        FloatRect originBounds(boundsOrigin(), size());
        FloatRect bounds = unionRect(zeroBounds, originBounds);
        auto viaZero = intersection(visible, zeroBounds);
        auto viaOrigin = intersection(visible, originBounds);
        auto wanted = intersection(visible, bounds);
        for (auto& animation : m_animations) {
            if (animation.value.state.property == 2)
                wanted = bounds;
        }
        bool outside = !m_coverage.contains(wanted);
        if ((drawsContent() || m_solid.isValid() || hasCanvasFrame()) && !wanted.isEmpty() && (m_damage.full || m_damage.count || outside || !m_picture)) {
            // coverage follows the visible region, retaining offscreen commands is bounded by the viewport.
            // visible is one screen each side: half a screen more makes the next recording due after half a
            // screen of scrolling instead of 128 points, and each recording paints the whole region on the
            // core thread, which a fling on a two core phone cannot afford every few frames. the raster reach
            // stays one screen and the extra half keeps it covered until the next recording
            wanted.inflate(FloatSize(128, visible.height() / 6 + 128));
            wanted.intersect(bounds);
            SkPictureRecorder recorder;
            SkRTreeFactory boxes;
            auto* canvas = recorder.beginRecording(SkRect::MakeXYWH(wanted.x(), wanted.y(), wanted.width(), wanted.height()), &boxes);
            if (m_solid.isValid()) {
                SkPaint paint;
                paint.setColor(static_cast<SkColor>(m_solid));
                auto rect = contentsRect();
                canvas->drawRect(SkRect::MakeXYWH(rect.x(), rect.y(), rect.width(), rect.height()), paint);
            }
#if ENABLE(WEBGL)
            if (hasCanvasFrame()) {
                // the rect is empty until the backing sets one for contents that are not clipped: the frame then fills the layer
                auto rect = contentsRect().isEmpty() ? FloatRect(FloatPoint(), size()) : contentsRect();
                canvas->drawImageRect(m_canvasDelegate->frame(), SkRect::MakeXYWH(rect.x(), rect.y(), rect.width(), rect.height()), SkSamplingOptions(SkFilterMode::kLinear));
            }
#endif
            if (drawsContent()) {
                GraphicsContextSkia context(*canvas, RenderingMode::Unaccelerated, RenderingPurpose::LayerBacking);
                paintGraphicsLayerContents(context, wanted, { GraphicsLayerPaintBehavior::ForceSynchronousImageDecode });
            }
            // a stand for hosts that record much faster than a phone: YOZORA_RECORD_DELAY_MS_PER_1000 is the
            // time a recording takes per thousand points of height. zero outside the tests
            static const long delayPerThousand = [] {
                const char* text = std::getenv("YOZORA_RECORD_DELAY_MS_PER_1000");
                long parsed = text ? std::atol(text) : 0;
                return parsed < 0 ? 0 : (parsed > 2000 ? 2000 : parsed);
            }();
            if (delayPerThousand)
                usleep(static_cast<useconds_t>(delayPerThousand * wanted.height()));
            m_picture = recorder.finishRecordingAsPicture();
            m_coverage = wanted;
            ++m_version;
            m_changes.push_back({ m_version, m_damage });
            if (m_changes.size() > 8)
                m_changes.erase(m_changes.begin());
            m_damage = { };
        }
        traceBounds(visible, viaZero, viaOrigin);
        result.version = m_version;
        result.coverage = SkRect::MakeXYWH(m_coverage.x(), m_coverage.y(), m_coverage.width(), m_coverage.height());
        result.picture = m_picture;
        result.changes = m_changes;
        static bool trace = std::getenv("YOZORA_TRACE_RENDERING");
        if (trace)
            std::fprintf(stderr, "yozora-scene: id=%llu parent=%llu x=%g y=%g width=%g height=%g draws=%d hidden=%d clips=%d visible=%g,%g,%g,%g version=%llu animations=%zu\n",
                static_cast<unsigned long long>(state.id), static_cast<unsigned long long>(state.parent), state.x, state.y, state.width, state.height,
                drawsContent(), state.hidden, state.clips, visible.x(), visible.y(), visible.width(), visible.height(), static_cast<unsigned long long>(m_version), m_animations.size());
        for (auto& animation : m_animations) {
            auto copy = animation.value;
            if (!copy.state.paused)
                copy.state.elapsed = (MonotonicTime::now() - animation.begin).seconds();
            copy.captured = std::chrono::steady_clock::now();
            result.animations.push_back(WTF::move(copy));
        }
        return result;
    }
private:
    // one line in the app log for a layer whose bounds start away from zero, when what it records or what it
    // shows changed by more than a few points: where the layer is, how it is clipped, what of it the page shows
    // under each reading of the bounds and what the picture covers. the numbers are in the layer's own space.
    // the first lines of a run only, a scroll changes them all the time
    void traceBounds(const FloatRect& visible, const FloatRect& viaZero, const FloatRect& viaOrigin)
    {
        if (boundsOrigin().isZero())
            return;
        static std::atomic<unsigned> lines { 0 };
        if (lines.load() >= 300)
            return;
        auto step = [](float value) { return static_cast<int>(std::lround(value / 16)) * 16; };
        char text[400];
        std::snprintf(text, sizeof text,
            "id=%llu at %d,%d size %dx%d origin %d,%d clips=%d draws=%d hidden=%d visible=%d,%d %dx%d zero=%d,%d %dx%d origin=%d,%d %dx%d covers=%d,%d %dx%d picture=%d",
            static_cast<unsigned long long>(m_id), step(position().x()), step(position().y()), step(size().width()), step(size().height()),
            step(boundsOrigin().x()), step(boundsOrigin().y()), masksToBounds(), drawsContent(), !contentsAreVisible(),
            step(visible.x()), step(visible.y()), step(visible.width()), step(visible.height()),
            step(viaZero.x()), step(viaZero.y()), step(viaZero.width()), step(viaZero.height()),
            step(viaOrigin.x()), step(viaOrigin.y()), step(viaOrigin.width()), step(viaOrigin.height()),
            step(m_coverage.x()), step(m_coverage.y()), step(m_coverage.width()), step(m_coverage.height()), m_picture ? 1 : 0);
        if (m_lastBoundsTrace == text)
            return;
        m_lastBoundsTrace = text;
        ++lines;
        yozora_log(YOZORA_LOG_INFO, "layer", "%s", text);
    }
    std::string m_lastBoundsTrace;
    struct RunningAnimation {
        String name;
        Yozora::RecordedLayer::Animation value;
        MonotonicTime begin;
        bool explicitly_paused { false };
    };
    std::vector<RunningAnimation> m_animations;
    static std::atomic<uint64_t> nextID;
    const uint64_t m_id;
    uint64_t m_version { 0 };
    yozora_damage m_damage { };
    Color m_solid;
#if ENABLE(WEBGL)
    RefPtr<YozoraGLDisplayDelegate> m_canvasDelegate;
    bool hasCanvasFrame() const { return m_canvasDelegate && m_canvasDelegate->frame(); }
    bool hasCanvasSurface() const { return m_canvasDelegate && m_canvasDelegate->surface(); }
#else
    bool hasCanvasFrame() const { return false; }
    bool hasCanvasSurface() const { return false; }
#endif
    FloatRect m_coverage;
    sk_sp<SkPicture> m_picture;
    std::vector<Yozora::RecordedLayer::Change> m_changes;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(YozoraGraphicsLayer);
std::atomic<uint64_t> YozoraGraphicsLayer::nextID { 0 };

class YozoraGraphicsLayerFactory final : public GraphicsLayerFactory {
public:
    Ref<GraphicsLayer> createGraphicsLayer(GraphicsLayerType type, GraphicsLayerClient& client) final
    {
        return adoptRef(*new YozoraGraphicsLayer(type, client));
    }
};

GraphicsLayerFactory& yozoraGraphicsLayerFactory()
{
    static NeverDestroyed<YozoraGraphicsLayerFactory> factory;
    return factory;
}

void yozoraPurgeScene(GraphicsLayer* layer)
{
    // from an explicit stack, like the recording: the depth of a page is not bounded
    std::vector<GraphicsLayer*> pending;
    if (layer)
        pending.push_back(layer);
    while (!pending.empty()) {
        auto* current = pending.back();
        pending.pop_back();
        static_cast<YozoraGraphicsLayer*>(current)->purge();
        if (current->maskLayer())
            pending.push_back(current->maskLayer());
        for (auto& child : current->children())
            pending.push_back(child.ptr());
    }
}

Ref<GraphicsLayer> GraphicsLayer::create(GraphicsLayerFactory* factory, GraphicsLayerClient& client, Type type)
{
    return (factory ? *factory : yozoraGraphicsLayerFactory()).createGraphicsLayer(type, client);
}

// layers are recorded parent first, a mask before its owner's children, from an explicit stack: a page may
// nest layers as deep as it likes and thread stack recursion would be the limit
uint64_t yozoraGraphicsLayerId(GraphicsLayer* layer)
{
    return layer ? static_cast<YozoraGraphicsLayer*>(layer)->id() : 0;
}

static void recordLayers(GraphicsLayer& root, const FloatRect& viewport, Yozora::Scene& scene)
{
    struct Pending {
        GraphicsLayer* layer;
        TransformationMatrix parentTransform;
        uint64_t parentID;
    };
    std::vector<Pending> pending;
    pending.push_back({ &root, TransformationMatrix(), 0 });
    while (!pending.empty()) {
        auto [current, parentTransform, parentID] = std::move(pending.back());
        pending.pop_back();
        auto& layer = *current;
        auto transform = parentTransform;
        auto anchor = layer.anchorPoint();
        float ax = anchor.x() * layer.size().width(), ay = anchor.y() * layer.size().height();
        transform.translate3d(layer.position().x() + ax, layer.position().y() + ay, anchor.z());
        transform.multiply(layer.transform());
        transform.translate3d(-ax - layer.boundsOrigin().x(), -ay - layer.boundsOrigin().y(), -anchor.z());
        auto inverse = transform.inverse();
        auto visible = inverse ? inverse->mapRect(viewport) : FloatRect();
        scene.layers.push_back(static_cast<YozoraGraphicsLayer&>(layer).record(visible));
        auto& state = scene.layers.back().state;
        state.parent = parentID;
        uint64_t id = state.id;
        // a mask uses its owner's coordinates, the child transform only affects descendants
        auto childTransform = transform;
        childTransform.multiply(layer.childrenTransform());
        auto& children = layer.children();
        for (size_t i = children.size(); i-- > 0;)
            pending.push_back({ children[i].ptr(), childTransform, id });
        if (layer.maskLayer())
            pending.push_back({ layer.maskLayer(), transform, id });
    }
}

std::shared_ptr<Yozora::Scene> yozoraRecordScene(GraphicsLayer* root, LocalFrameView& view, unsigned width, unsigned height, float scale, uint64_t generation)
{
    auto scene = std::make_shared<Yozora::Scene>();
    scene->generation = generation;
    scene->width = width;
    scene->height = height;
    scene->scale = scale;
    scene->scroll_y = view.scrollPosition().y();
    if (!root)
        return scene;
    FloatRect viewport(0, -static_cast<float>(height), width, height * 3);
    recordLayers(*root, viewport, *scene);
    auto* render = view.renderView();
    if (!render)
        return scene;
    auto* scrolling = render->compositor().scrolledContentsLayer();
    auto* clipping = render->compositor().layerForClipping();
    for (auto& layer : scene->layers) {
        if (clipping && layer.state.id == static_cast<YozoraGraphicsLayer*>(clipping)->id())
            layer.state.viewport_clip = 1;
        if (scrolling && layer.state.id == static_cast<YozoraGraphicsLayer*>(scrolling)->id()) {
            layer.state.scroll_id = 1;
            layer.state.scroll_y = scene->scroll_y;
        }
    }
    if (auto* areas = view.scrollableAreas()) {
        for (auto& area : *areas) {
            if (!area.isRenderLayer())
                continue;
            auto& scrollable = uncheckedDowncast<RenderLayerScrollableArea>(area);
            auto* backing = scrollable.layer().backing();
            auto* contents = backing ? backing->scrolledContentsLayer() : nullptr;
            if (!contents)
                continue;
            auto id = static_cast<YozoraGraphicsLayer*>(contents)->id();
            for (auto& layer : scene->layers) {
                if (layer.state.id == id) {
                    layer.state.scroll_id = reinterpret_cast<uint64_t>(&area);
                    layer.state.scroll_x = area.scrollOffset().x();
                    layer.state.scroll_y = area.scrollOffset().y();
                }
            }
        }
    }
    if (auto* objects = view.viewportConstrainedObjects()) {
        for (auto& object : *objects) {
            if (!object.layer() || !object.layer()->backing())
                continue;
            auto* backing = object.layer()->backing();
            auto* anchor = backing->viewportClippingOrAnchorLayer();
            if (!anchor)
                anchor = backing->graphicsLayer();
            if (!anchor)
                continue;
            auto id = static_cast<YozoraGraphicsLayer*>(anchor)->id();
            auto found = std::find_if(scene->layers.begin(), scene->layers.end(), [&](auto& layer) { return layer.state.id == id; });
            if (found == scene->layers.end())
                continue;
            if (object.isFixedPositioned() && object.container() == render && scrolling && anchor->hasAncestor(scrolling))
                found->state.fixed = 1;
            auto* box = dynamicDowncast<RenderBoxModelObject>(&object);
            if (!box || !box->isStickilyPositioned())
                continue;
            auto rect = box->constrainingRectForStickyPosition();
            StickyPositionViewportConstraints constraints;
            box->computeStickyPositionConstraints(constraints, rect);
            auto& sticky = found->state.sticky;
            sticky.scroll_id = 1;
            sticky.scroll_y = scene->scroll_y;
            auto [clipBox, clipLayer] = box->enclosingClippingBoxForStickyPosition();
            if (clipLayer && clipLayer->scrollableArea()) {
                auto* area = clipLayer->scrollableArea();
                sticky.scroll_id = reinterpret_cast<uint64_t>(area);
                sticky.scroll_x = area->scrollOffset().x();
                sticky.scroll_y = area->scrollOffset().y();
            }
            sticky.edges = constraints.anchorEdges();
            auto block = constraints.containingBlockRect();
            auto stickyBox = constraints.stickyBoxRect();
            for (auto pair : { std::pair { sticky.viewport, rect }, std::pair { sticky.container, block }, std::pair { sticky.box, stickyBox } }) {
                pair.first[0] = pair.second.x();
                pair.first[1] = pair.second.y();
                pair.first[2] = pair.second.width();
                pair.first[3] = pair.second.height();
            }
            sticky.offsets[0] = constraints.leftOffset();
            sticky.offsets[1] = constraints.rightOffset();
            sticky.offsets[2] = constraints.topOffset();
            sticky.offsets[3] = constraints.bottomOffset();
        }
    }
    if (std::getenv("YOZORA_TRACE_RENDERING")) {
        for (auto& layer : scene->layers) {
            if (layer.state.fixed || layer.state.sticky.scroll_id)
                std::fprintf(stderr, "yozora-constraint: id=%llu fixed=%d sticky=%llu\n",
                    static_cast<unsigned long long>(layer.state.id), layer.state.fixed,
                    static_cast<unsigned long long>(layer.state.sticky.scroll_id));
        }
    }
    return scene;
}

}
