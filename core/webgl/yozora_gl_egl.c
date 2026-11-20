/* the OpenGL ES context on the host: EGL with no surface, which Mesa gives on a machine with no display. used
   by the host build and its tests */
#define _GNU_SOURCE
#include "yozora_gl_internal.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct yozora_gl_context {
    yozora_gl_state state;
    EGLContext context;
};

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static __thread yozora_gl_context *current_context;
static pthread_once_t display_once = PTHREAD_ONCE_INIT;
static EGLDisplay display = EGL_NO_DISPLAY;
static void *driver;

static void open_display(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (platform)
        display = platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (display == EGL_NO_DISPLAY)
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display != EGL_NO_DISPLAY && !eglInitialize(display, &major, &minor))
        display = EGL_NO_DISPLAY;
    /* the core functions of ES 2.0 and 3.0 are exported by the library of the API, the extensions come from eglGetProcAddress */
    driver = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
}

yozora_gl_state *yozora_gl_current_state(void)
{
    return current_context ? &current_context->state : NULL;
}

yozora_gl_context *yozora_gl_context_create(int version)
{
    if (version != 2 && version != 3)
        return NULL;
    pthread_once(&display_once, open_display);
    if (display == EGL_NO_DISPLAY || !eglBindAPI(EGL_OPENGL_ES_API))
        return NULL;
    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, version == 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
        return NULL;
    const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE };
    EGLContext handle = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (handle == EGL_NO_CONTEXT)
        return NULL;
    yozora_gl_context *context = calloc(1, sizeof *context);
    if (!context) {
        eglDestroyContext(display, handle);
        return NULL;
    }
    context->context = handle;
    context->state.version = version;
    context->state.pack_alignment = 4;
    context->state.unpack_alignment = 4;
    return context;
}

int yozora_gl_context_version(const yozora_gl_context *context)
{
    return context ? context->state.version : 0;
}

int yozora_gl_context_make_current(yozora_gl_context *context)
{
    if (!context)
        return 0;
    if (current_context == context)
        return 1;
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context->context))
        return 0;
    current_context = context;
    return 1;
}

void yozora_gl_context_release_thread(void)
{
    if (display != EGL_NO_DISPLAY)
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    current_context = NULL;
}

void yozora_gl_context_destroy(yozora_gl_context *context)
{
    if (!context)
        return;
    if (current_context == context)
        yozora_gl_context_release_thread();
    eglDestroyContext(display, context->context);
    free(context->state.extensions);
    free(context);
}

void *yozora_gl_proc(const char *name)
{
    pthread_once(&display_once, open_display);
    void *function = driver ? dlsym(driver, name) : NULL;
    if (function)
        return function;
    /* Mesa answers eglGetProcAddress for a name it never heard of with a function that does nothing, so only
       vendor extension names go there */
    size_t length = strlen(name);
    static const char *const suffixes[] = { "EXT", "OES", "KHR", "NV", "APPLE", "ANGLE", "IMG", "QCOM" };
    for (size_t i = 0; i < sizeof suffixes / sizeof *suffixes; ++i) {
        size_t size = strlen(suffixes[i]);
        if (length > size && !strcmp(name + length - size, suffixes[i]))
            return (void *)eglGetProcAddress(name);
    }
    return NULL;
}

/* the host has no layer to present into: the caller reads the frame back, which is what the host tests check */
yozora_gl_surface *yozora_gl_surface_create(yozora_gl_context *page, unsigned width, unsigned height, int opaque, int premultiplied)
{
    (void)page;
    (void)width;
    (void)height;
    (void)opaque;
    (void)premultiplied;
    return NULL;
}

yozora_canvas *yozora_gl_surface_canvas(yozora_gl_surface *surface)
{
    (void)surface;
    return NULL;
}

int yozora_gl_surface_present(yozora_gl_surface *surface, unsigned texture)
{
    (void)surface;
    (void)texture;
    return 0;
}

void yozora_gl_surface_close(yozora_gl_surface *surface)
{
    (void)surface;
}
