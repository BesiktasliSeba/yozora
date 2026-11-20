#include "config.h"
#include "GraphicsContextGLYozora.h"

#if ENABLE(WEBGL)

#include "ANGLEHeaders.h"
#include "ANGLEUtilities.h"
#include "ImageBuffer.h"
#include "PixelBuffer.h"
#include "yozora_gl.h"
#include "../yozora_log.h"
#include <skia/core/SkData.h>
#include <skia/core/SkImageInfo.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(GraphicsContextGLYozora);

PlatformLayer* YozoraGLDisplayDelegate::tag()
{
    static PlatformLayer marker;
    return &marker;
}

// the work of a context WebKit leaves to the platform. ANGLE would delete these resources with the context, a
// driver is told to
GraphicsContextGLANGLE::~GraphicsContextGLANGLE()
{
    if (!makeContextCurrent()) {
        yozora_gl_context_destroy(static_cast<yozora_gl_context*>(m_contextObj));
        return;
    }

    if (m_texture)
        GL_DeleteTextures(1, &m_texture);

    auto attributes = contextAttributes();

    if (attributes.antialias) {
        GL_DeleteRenderbuffers(1, &m_multisampleColorBuffer);
        if (attributes.stencil || attributes.depth)
            GL_DeleteRenderbuffers(1, &m_multisampleDepthStencilBuffer);
        GL_DeleteFramebuffers(1, &m_multisampleFBO);
    } else {
        if ((attributes.stencil || attributes.depth) && m_depthStencilBuffer)
            GL_DeleteRenderbuffers(1, &m_depthStencilBuffer);
        if (m_preserveDrawingBufferTexture)
            GL_DeleteTextures(1, &m_preserveDrawingBufferTexture);
        if (m_preserveDrawingBufferFBO)
            GL_DeleteFramebuffers(1, &m_preserveDrawingBufferFBO);
    }
    GL_DeleteFramebuffers(1, &m_fbo);
    // the context goes last, with the objects of this destructor, and no later call may find it
    yozora_gl_context_destroy(static_cast<yozora_gl_context*>(m_contextObj));
    m_contextObj = nullptr;
}

bool GraphicsContextGLANGLE::makeContextCurrent()
{
    return yozora_gl_context_make_current(static_cast<yozora_gl_context*>(m_contextObj));
}

void GraphicsContextGLANGLE::checkGPUStatus()
{
}

void GraphicsContextGLANGLE::platformReleaseThreadResources()
{
}

RefPtr<GraphicsContextGL> createWebProcessGraphicsContextGL(const GraphicsContextGLAttributes& attributes)
{
    return GraphicsContextGLYozora::create(GraphicsContextGLAttributes { attributes });
}

RefPtr<GraphicsContextGLYozora> GraphicsContextGLYozora::create(GraphicsContextGLAttributes&& attributes)
{
    auto context = adoptRef(*new GraphicsContextGLYozora(WTF::move(attributes)));
    if (!context->initialize())
        return nullptr;
    return context;
}

GraphicsContextGLYozora::GraphicsContextGLYozora(GraphicsContextGLAttributes&& attributes)
    : GraphicsContextGLANGLE(WTF::move(attributes))
{
}

// the layer of the canvas lives in the driver of this context and goes before it
GraphicsContextGLYozora::~GraphicsContextGLYozora()
{
    dropSurface();
}

void GraphicsContextGLYozora::dropSurface()
{
    if (!m_surface)
        return;
    yozora_gl_surface_close(m_surface);
    m_surface = nullptr;
    m_surfaceSize = { };
    if (m_delegate)
        m_delegate->setSurface(nullptr);
}

RefPtr<GraphicsLayerContentsDisplayDelegate> GraphicsContextGLYozora::layerContentsDisplayDelegate()
{
    return m_delegate;
}

bool GraphicsContextGLYozora::platformInitializeContext()
{
    m_isForWebGL2 = contextAttributes().isWebGL2;
    auto* gl = yozora_gl_context_create(m_isForWebGL2 ? 3 : 2);
    if (!gl)
        return false;
    m_contextObj = gl;
    // the display is a name for the place the context lives in, and has no other use here
    m_displayObj = EGL_GetCurrentDisplay();
    return makeContextCurrent();
}

bool GraphicsContextGLYozora::platformInitialize()
{
    m_delegate = YozoraGLDisplayDelegate::create();
    return true;
}

bool GraphicsContextGLYozora::reshapeDrawingBuffer()
{
    auto attributes = contextAttributes();
    const auto size = getInternalFramebufferSize();
    GLuint colorFormat = attributes.alpha ? GL_RGBA : GL_RGB;
    ScopedRestoreTextureBinding restoreBinding(TEXTURE_BINDING_2D, TEXTURE_2D);

    // m_fbo is bound by the caller: its color is this texture, or the texture of preserveDrawingBuffer which the caller attaches after
    GL_BindTexture(GL_TEXTURE_2D, m_texture);
    GL_TexImage2D(GL_TEXTURE_2D, 0, colorFormat, size.width(), size.height(), 0, colorFormat, GL_UNSIGNED_BYTE, nullptr);
    GL_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    if (m_preserveDrawingBufferFBO) {
        // the blit target of preserveDrawingBuffer keeps the texture that the frame is read from
        GL_BindFramebuffer(GL_FRAMEBUFFER, m_preserveDrawingBufferFBO);
        GL_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
        GL_BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    }
    return true;
}

#if ENABLE(VIDEO)
// a frame of a video would have to be uploaded from the memory of the decoder, which this port does not give to the page
bool GraphicsContextGLYozora::copyTextureFromVideoFrame(VideoFrame&, PlatformGLObject, GCGLenum, GCGLint, GCGLenum, GCGLenum, GCGLenum, bool, bool)
{
    return false;
}
#endif

RefPtr<PixelBuffer> GraphicsContextGLYozora::readCompositedResults()
{
    return readRenderingResultsForPainting();
}

// the frame goes from the texture of the drawing buffer into a native layer on the graphics chip. returns
// false when there is no such layer for this size, the caller reads the frame back then
bool GraphicsContextGLYozora::presentToSurface(const IntSize& size)
{
    if (m_surface && size != m_surfaceSize)
        dropSurface();
    if (!m_surface) {
        if (size == m_surfaceRefused)
            return false;
        auto attributes = contextAttributes();
        m_surface = yozora_gl_surface_create(static_cast<yozora_gl_context*>(m_contextObj), size.width(), size.height(), !attributes.alpha, attributes.premultipliedAlpha);
        if (!m_surface) {
            // the host has no layer to present into, a phone may refuse a renderbuffer of this size
            m_surfaceRefused = size;
            yozora_log(YOZORA_LOG_DEBUG, "webgl", "no native layer for a canvas of %dx%d, frames are read back", size.width(), size.height());
            return false;
        }
        m_surfaceSize = size;
        m_surfaceRefused = { };
        m_delegate->setSurface(yozora_gl_surface_canvas(m_surface));
    }
    if (contextAttributes().antialias)
        resolveMultisamplingIfNecessary();
    // the drawing buffer is this texture, or the one of preserveDrawingBuffer when the page keeps it
    if (yozora_gl_surface_present(m_surface, m_preserveDrawingBufferTexture ? m_preserveDrawingBufferTexture : m_texture))
        return true;
    yozora_log(YOZORA_LOG_WARN, "webgl", "a native layer of %dx%d failed to show a frame, frames are read back", size.width(), size.height());
    m_surfaceRefused = size;
    dropSurface();
    return false;
}

// the frame is shown in a native layer where the system has one, else (the host) there is no compositor
// to give a texture to: the frame is read back into a picture and the canvas layer draws it
void GraphicsContextGLYozora::prepareForDisplay()
{
    if (!m_delegate || !makeContextCurrent())
        return;
    auto size = getInternalFramebufferSize();
    if (size.isEmpty()) {
        dropSurface();
        m_delegate->setFrame(nullptr);
        return;
    }
    if (presentToSurface(size))
        return;
    auto pixels = readRenderingResults();
    if (!pixels) {
        m_delegate->setFrame(nullptr);
        return;
    }
    auto attributes = contextAttributes();
    size_t width = size.width(), height = size.height(), row = width * 4;
    auto bytes = pixels->bytes();
    if (bytes.size() < row * height)
        return;
    sk_sp<SkData> data = SkData::MakeUninitialized(row * height);
    if (!data)
        return;
    auto* out = static_cast<uint8_t*>(data->writable_data());
    // OpenGL reads from the bottom row up, a picture starts at the top
    for (size_t y = 0; y < height; ++y)
        std::memcpy(out + y * row, bytes.data() + (height - 1 - y) * row, row);
    auto alpha = !attributes.alpha ? kOpaque_SkAlphaType : (attributes.premultipliedAlpha ? kPremul_SkAlphaType : kUnpremul_SkAlphaType);
    m_delegate->setFrame(SkImages::RasterFromData(SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, alpha), data, row));
}

}

#endif
