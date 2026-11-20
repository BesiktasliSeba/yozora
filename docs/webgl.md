# webgl

webgl 1 and webgl 2, always on, there is no switch.

## how

webkit keeps its own context code, written against angle. angle itself is not built, only its headers are used: `core/webgl` makes the entry points of angle (`GL_*`) over the opengl es of the system.

- ios: eagl, es 2.0 on every device, es 3.0 from ios 7 on an a7 or later, so webgl 2 only there
- host: egl without a surface (mesa)
- the shim adds what angle did: the extension list, `*RobustANGLE` calls, reverse row order for reads, zeroed new buffers and texture images, `KHR_debug` that never fires

## the canvas

`core/webcore/GraphicsContextGLYozora.cpp` is the context. when the page asks to display, the frame goes one of two ways.

- ios, armv7 and arm64: the context makes a `CAEAGLLayer` of the size of the drawing buffer (`yozora_gl_surface_create`, `core/webgl/yozora_gl_eagl.m`) and a second EAGL context in the same sharegroup. after a `glFlush` of the page context that context draws the texture of the drawing buffer over the layer and presents it. no pixel crosses the processor and the layer of the canvas is not recorded again for a frame. `YZSceneView` puts the `CAEAGLLayer` into the contents layer of the canvas, over its tiles, and places it by position and scale, never by bounds, because the bounds are the pixels of the context. the scene holds a reference to it (`yozora_canvas`) for as long as it can be shown
- host: Mesa has no layer to present into, `yozora_gl_surface_create` answers NULL and the frame is read back with `glReadPixels` into a picture that `GraphicsLayerYozora` draws in the layer of the canvas. the same path takes over on a phone when the system refuses a layer or a frame, once per size of the canvas, and says so in the log

a native layer costs three buffers of the size of the canvas (the swap chain of Core Animation). a canvas of 2048 by 2048 holds 48 MB of them. `renderInContext` does not draw the layer, so a preview made that way shows the canvas empty.

## what a driver does not do

angle checked these, the driver does not, and a page that breaks a rule gets what the driver does with it:

- draw calls are not checked against the rules of webgl: out of range indices, feedback loops, sampler types
- renderbuffers and `texStorage` images are not zeroed
- no antialiasing on es 2.0
- no timer queries
- no `OffscreenCanvas` or webgl in workers

webgl in a video texture is not done.

## debug

`YOZORA_GL_TRACE=1` names every forwarded call that left an error of the driver. it reads the error itself, so the page never sees it.

## tests

- `tests/test_gles_shim.cpp`: the entry points on a host driver
- `tests/test_core_webgl.py`: a page of webgl 1 and of webgl 2 drawn and read back, and the canvas in the picture of the page
- `tests/test_scene_raster.cpp` (`testCanvasSurface`): a native canvas layer reaches the scene with its rectangle and no tile, and the scene keeps the surface alive after the recording is gone
- `tests/test_core_webgl_option.py`: the option of the core
