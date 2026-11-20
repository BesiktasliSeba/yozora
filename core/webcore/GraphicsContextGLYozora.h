#pragma once

#include <wtf/Platform.h>

#if ENABLE(WEBGL)

#include "GraphicsContextGLANGLE.h"
#include "GraphicsLayerContentsDisplayDelegate.h"
#include "../core/yozora_canvas.h"
#include <skia/core/SkImage.h>
#include <wtf/Function.h>

typedef struct yozora_gl_context yozora_gl_context;
typedef struct yozora_gl_surface yozora_gl_surface;

namespace WebCore {

// what a canvas layer shows: a native layer that the context presents its frames into, or where the system has
// none (the host) the last frame the context read back, as a picture the raster thread can keep.
// the context writes either on the page thread and the layer reads it there when it records, so the two never
// meet on another thread
class YozoraGLDisplayDelegate final : public GraphicsLayerContentsDisplayDelegate {
public:
    static Ref<YozoraGLDisplayDelegate> create() { return adoptRef(*new YozoraGLDisplayDelegate); }
    // a layer takes a delegate for its own only when this is what platformLayer answers, the port has no other kind
    static PlatformLayer* NODELETE tag();
    PlatformLayer* platformLayer() const final { return tag(); }

    ~YozoraGLDisplayDelegate()
    {
        if (m_surface)
            m_surface->release(m_surface);
    }

    // the layer that shows the frame asks to be recorded again when a new one is there, there is no compositor to pull it
    void setObserver(Function<void()>&& observer) { m_observer = WTF::move(observer); }
    void setFrame(sk_sp<SkImage>&& frame)
    {
        m_frame = WTF::move(frame);
        if (m_observer)
            m_observer();
    }
    const sk_sp<SkImage>& frame() const { return m_frame; }
    // the delegate takes over the reference of the caller. a surface shows every frame by itself, so the layer
    // is told only when the surface changes, not per frame
    void setSurface(yozora_canvas* surface)
    {
        if (m_surface)
            m_surface->release(m_surface);
        m_surface = surface;
        m_frame = nullptr;
        if (m_observer)
            m_observer();
    }
    yozora_canvas* surface() const { return m_surface; }

private:
    YozoraGLDisplayDelegate() = default;
    sk_sp<SkImage> m_frame;
    yozora_canvas* m_surface { nullptr };
    Function<void()> m_observer;
};

// WebGL over the OpenGL ES of the system (EAGL on iOS, EGL on the host), through the entry points of ANGLE that core/webgl makes
class GraphicsContextGLYozora final : public GraphicsContextGLANGLE {
    WTF_MAKE_TZONE_ALLOCATED(GraphicsContextGLYozora);
public:
    static RefPtr<GraphicsContextGLYozora> create(GraphicsContextGLAttributes&&);
    ~GraphicsContextGLYozora() final;

    RefPtr<GraphicsLayerContentsDisplayDelegate> layerContentsDisplayDelegate() final;
#if ENABLE(VIDEO)
    bool copyTextureFromVideoFrame(VideoFrame&, PlatformGLObject, GCGLenum, GCGLint, GCGLenum, GCGLenum, GCGLenum, bool, bool) final;
#endif
    RefPtr<PixelBuffer> readCompositedResults() final;
    bool reshapeDrawingBuffer() final;
    void prepareForDisplay() final;

private:
    explicit GraphicsContextGLYozora(GraphicsContextGLAttributes&&);
    bool platformInitializeContext() final;
    bool platformInitialize() final;
    bool presentToSurface(const IntSize&);
    void dropSurface();

    RefPtr<YozoraGLDisplayDelegate> m_delegate;
    // the native layer of the canvas and the size it was made for. a size that the system refused is not tried
    // again until the canvas changes size, so a frame is not spent on a layer every time
    yozora_gl_surface* m_surface { nullptr };
    IntSize m_surfaceSize;
    IntSize m_surfaceRefused;
};

}

#endif
