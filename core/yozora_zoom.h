#ifndef YOZORA_ZOOM_H
#define YOZORA_ZOOM_H

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_ZOOM_MIN 50
#define YOZORA_ZOOM_DEFAULT 100
#define YOZORA_ZOOM_MAX 300
/* the floor of the zoom a page asks for with a viewport tag: a page that wants 1280 points on a 320 point view needs 25 percent */
#define YOZORA_ZOOM_FIT_MIN 25

/* a page zoom in percent held to the supported range */
int yozora_zoom_clamp(int percent);

/* a page zoom in percent held to the range the viewport tag may use, below the floor of the zoom a person sets */
int yozora_zoom_clamp_fit(int percent);

/* the page zoom that fits a page with <meta name=viewport content=width=N> on a view of view_width points:
   the page lays out N wide and is scaled to the view. 100 for a width that is not positive or does not exceed
   the view (device-width, auto, a narrower page) */
int yozora_zoom_for_viewport(int view_width, double meta_width);

/* unset values and device-width as ViewportArguments holds them */
#define YOZORA_VIEWPORT_AUTO (-1)
#define YOZORA_VIEWPORT_DEVICE_WIDTH (-2)

/* layout width Safari gives a page with a viewport tag, 0 when it sets neither a width nor an initial scale */
double yozora_zoom_viewport_width(int view_width, double width, double initial_scale, double min_scale, double max_scale);

/* the scroll position that keeps the page row under focal (a distance from the view top) under it after the
   zoom changed from old_percent to new_percent, held between minimum and maximum, the page limits at the new
   zoom */
int yozora_zoom_anchor_scroll(int scroll, int focal, int old_percent, int new_percent, int minimum, int maximum);


/* the zoom of Safari: the page keeps its layout and a pinch enlarges its picture. the view shows the part of
   the viewport at (x, y), in viewport points, 1 / scale of its size. a vertical move that leaves the viewport
   goes back to the caller as a page scroll */
#define YOZORA_VIEW_ZOOM_MAX 3.0f

typedef struct yozora_view_zoom {
    float scale;
    float x;
    float y;
} yozora_view_zoom;

/* scale 1, nothing enlarged */
void yozora_view_zoom_reset(yozora_view_zoom *zoom);

/* a scale held to 1 .. YOZORA_VIEW_ZOOM_MAX, a value that is not a number gives 1 */
float yozora_view_zoom_clamp(float scale);

/* the zoom a pinch gives: it began at start with fingers at (start_x, start_y), they are at (focal_x,
   focal_y) and spread by scale. the viewport point under the fingers stays under them. a vertical offset that
   would leave the viewport is held and its distance in viewport points goes to scroll_y (negative is up) */
void yozora_view_zoom_pinch(const yozora_view_zoom *start, float start_x, float start_y, float scale, float focal_x, float focal_y,
    float width, float height, yozora_view_zoom *out, float *scroll_y);

/* a drag by (dx, dy) screen points moves the view over the viewport. what the viewport has no room for goes
   to rest_x and rest_y in viewport points, the vertical rest is for the page to scroll. at scale 1 the rest
   is the drag itself */
void yozora_view_zoom_pan(yozora_view_zoom *zoom, float dx, float dy, float width, float height, float *rest_x, float *rest_y);

/* the point of the screen to the point of the viewport under it, and back */
void yozora_view_zoom_to_viewport(const yozora_view_zoom *zoom, float x, float y, float *out_x, float *out_y);
void yozora_view_zoom_to_screen(const yozora_view_zoom *zoom, float x, float y, float *out_x, float *out_y);

/* the factor tiles are painted at on top of the screen scale: the zoom in steps of a half so a pinch does not
   repaint every frame, never below 1, never above max_factor */
float yozora_view_zoom_raster_factor(float scale, float max_factor);

#ifdef __cplusplus
}
#endif
#endif
