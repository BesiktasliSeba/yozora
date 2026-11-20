// the EGL calls GraphicsContextGLANGLE makes outside context creation: which display is current, making a
// context current or none, ending use of a display. the display is the one place a context lives in and a
// context handle is the yozora_gl_context
#define EGL_EGL_PROTOTYPES 0
#include <entry_points_egl_autogen.h>
#include <entry_points_egl_ext_autogen.h>

#include "yozora_gl.h"

namespace {
char displayMarker;
}

extern "C" {

ANGLE_EXPORT EGLDisplay EGLAPIENTRY EGL_GetCurrentDisplay()
{
    return &displayMarker;
}

ANGLE_EXPORT EGLBoolean EGLAPIENTRY EGL_MakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext ctx)
{
    if (ctx == EGL_NO_CONTEXT) {
        yozora_gl_context_release_thread();
        return EGL_TRUE;
    }
    return yozora_gl_context_make_current(static_cast<yozora_gl_context*>(ctx)) ? EGL_TRUE : EGL_FALSE;
}

ANGLE_EXPORT EGLBoolean EGLAPIENTRY EGL_Terminate(EGLDisplay)
{
    return EGL_TRUE;
}

ANGLE_EXPORT EGLBoolean EGLAPIENTRY EGL_ReleaseThread()
{
    yozora_gl_context_release_thread();
    return EGL_TRUE;
}

// images and sync objects of EGL are made only for WebXR and for video frames, which this port does not have
ANGLE_EXPORT EGLBoolean EGLAPIENTRY EGL_DestroyImageKHR(EGLDisplay, EGLImageKHR)
{
    return EGL_FALSE;
}

ANGLE_EXPORT EGLBoolean EGLAPIENTRY EGL_DestroySync(EGLDisplay, EGLSync)
{
    return EGL_FALSE;
}

}
