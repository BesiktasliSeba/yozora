#ifndef YOZORA_CANVAS_H
#define YOZORA_CANVAS_H

#ifdef __cplusplus
extern "C" {
#endif

/* a native layer that a WebGL canvas presents its frames into, so the pixels never cross the processor. the
   scene shows `layer` (a CAEAGLLayer on iOS) inside the layer of the canvas and never touches its bounds: the
   context that draws into it sized it once. `retain` and `release` count the owners, the scene holds one for
   as long as it can be shown. the struct is the first member of what its owner allocates */
typedef struct yozora_canvas yozora_canvas;
struct yozora_canvas {
    void *layer;
    unsigned width, height;
    void (*retain)(yozora_canvas *);
    void (*release)(yozora_canvas *);
};

#ifdef __cplusplus
}
#endif
#endif
