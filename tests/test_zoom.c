#include "yozora_zoom.h"
#include <math.h>
#include <stdio.h>

static int failures;

static int near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_zoom: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(void)
{
    CHECK(yozora_zoom_clamp(10) == YOZORA_ZOOM_MIN);
    CHECK(yozora_zoom_clamp(999) == YOZORA_ZOOM_MAX);
    CHECK(yozora_zoom_clamp(125) == 125);
    CHECK(yozora_zoom_clamp_fit(5) == YOZORA_ZOOM_FIT_MIN);
    CHECK(yozora_zoom_clamp_fit(37) == 37);
    CHECK(yozora_zoom_clamp_fit(999) == YOZORA_ZOOM_MAX);

    /* width=850 (Speedometer 3.0) on a 320 point view is shown at 37 percent, below the floor of a person's zoom */
    CHECK(yozora_zoom_for_viewport(320, 850) == 37);
    CHECK(yozora_zoom_for_viewport(390, 850) == 45);
    /* a width that does not exceed the view, or is not a width, leaves the page alone */
    CHECK(yozora_zoom_for_viewport(320, 320) == 100);
    CHECK(yozora_zoom_for_viewport(768, 640) == 100);
    CHECK(yozora_zoom_for_viewport(320, -2) == 100);
    CHECK(yozora_zoom_for_viewport(320, 0) == 100);
    CHECK(yozora_zoom_for_viewport(320, NAN) == 100);
    CHECK(yozora_zoom_for_viewport(0, 850) == 100);
    /* a very wide page stops at the floor, a huge value is bounded */
    CHECK(yozora_zoom_for_viewport(320, 5000) == YOZORA_ZOOM_FIT_MIN);
    CHECK(yozora_zoom_for_viewport(320, 1e300) == YOZORA_ZOOM_FIT_MIN);

    /* neoalpha.net: the later width=device-width wins, scale 0.5 */
    CHECK(yozora_zoom_viewport_width(320, YOZORA_VIEWPORT_DEVICE_WIDTH, 0.5, YOZORA_VIEWPORT_AUTO, 0.5) == 640);
    CHECK(yozora_zoom_for_viewport(320, yozora_zoom_viewport_width(320, YOZORA_VIEWPORT_DEVICE_WIDTH, 0.5, YOZORA_VIEWPORT_AUTO, 0.5)) == 50);
    CHECK(yozora_zoom_viewport_width(390, YOZORA_VIEWPORT_DEVICE_WIDTH, 0.5, YOZORA_VIEWPORT_AUTO, 0.5) == 780);
    CHECK(yozora_zoom_viewport_width(320, YOZORA_VIEWPORT_AUTO, 0.5, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO) == 640);
    CHECK(yozora_zoom_viewport_width(320, YOZORA_VIEWPORT_DEVICE_WIDTH, 1, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO) == 320);
    CHECK(yozora_zoom_viewport_width(320, YOZORA_VIEWPORT_DEVICE_WIDTH, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO) == 320);
    CHECK(yozora_zoom_viewport_width(320, 850, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO) == 850);
    CHECK(yozora_zoom_for_viewport(320, yozora_zoom_viewport_width(320, 850, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO)) == 37);
    CHECK(yozora_zoom_viewport_width(320, YOZORA_VIEWPORT_AUTO, YOZORA_VIEWPORT_AUTO, 1, 3) == 0);
    CHECK(yozora_zoom_viewport_width(0, 640, 0.5, 0.5, 0.5) == 0);
    CHECK(yozora_zoom_viewport_width(320, NAN, NAN, NAN, NAN) == 0);

    /* the row under the finger stays under it */
    CHECK(yozora_zoom_anchor_scroll(0, 0, 100, 200, 0, 5000) == 0);
    CHECK(yozora_zoom_anchor_scroll(1000, 300, 100, 200, 0, 5000) == 2300);
    CHECK(yozora_zoom_anchor_scroll(1000, 300, 200, 100, 0, 5000) == 350);
    /* limits of the new page hold */
    CHECK(yozora_zoom_anchor_scroll(1000, 300, 100, 300, 0, 1500) == 1500);
    CHECK(yozora_zoom_anchor_scroll(10, 600, 200, 50, 0, 5000) == 0);
    CHECK(yozora_zoom_anchor_scroll(100, 100, 0, 100, 0, 5000) == 100);
    CHECK(yozora_zoom_anchor_scroll(100, 100, 100, 100, 50, 20) == 50);
    /* view zoom: scale is held to 1 .. 3, junk gives 1 */
    CHECK(near(yozora_view_zoom_clamp(0.5f), 1.0f));
    CHECK(near(yozora_view_zoom_clamp(2.0f), 2.0f));
    CHECK(near(yozora_view_zoom_clamp(9.0f), YOZORA_VIEW_ZOOM_MAX));
    CHECK(near(yozora_view_zoom_clamp(NAN), 1.0f));
    CHECK(near(yozora_view_zoom_clamp(-3.0f), 1.0f));

    yozora_view_zoom start, zoom;
    float scroll = 99, rest_x = 99, rest_y = 99, x, y;
    yozora_view_zoom_reset(&start);
    /* a pinch about the middle of a 320 x 480 viewport keeps the middle under the fingers */
    yozora_view_zoom_pinch(&start, 160, 240, 2.0f, 160, 240, 320, 480, &zoom, &scroll);
    CHECK(near(zoom.scale, 2.0f) && near(zoom.x, 80) && near(zoom.y, 120) && near(scroll, 0));
    yozora_view_zoom_to_viewport(&zoom, 160, 240, &x, &y);
    CHECK(near(x, 160) && near(y, 240));
    /* the corner of the screen shows the corner of the part that is on view, and back */
    yozora_view_zoom_to_viewport(&zoom, 0, 0, &x, &y);
    CHECK(near(x, 80) && near(y, 120));
    yozora_view_zoom_to_screen(&zoom, 80, 120, &x, &y);
    CHECK(near(x, 0) && near(y, 0));
    yozora_view_zoom_to_screen(&zoom, 240, 360, &x, &y);
    CHECK(near(x, 320) && near(y, 480));
    /* fingers at the top left corner: nothing is left of it, the zoom grows from the corner */
    yozora_view_zoom_pinch(&start, 0, 0, 3.0f, 0, 0, 320, 480, &zoom, &scroll);
    CHECK(near(zoom.scale, 3.0f) && near(zoom.x, 0) && near(zoom.y, 0) && near(scroll, 0));
    /* the fingers at the right edge: the view stops at the right edge of the viewport */
    yozora_view_zoom_pinch(&start, 320, 480, 2.0f, 320, 480, 320, 480, &zoom, &scroll);
    CHECK(near(zoom.x, 160) && near(zoom.y, 240));
    /* a scale past the limit stops at it, one that is no number leaves the zoom */
    yozora_view_zoom_pinch(&start, 160, 240, 10.0f, 160, 240, 320, 480, &zoom, NULL);
    CHECK(near(zoom.scale, YOZORA_VIEW_ZOOM_MAX));
    yozora_view_zoom_pinch(&start, 160, 240, NAN, 160, 240, 320, 480, &zoom, NULL);
    CHECK(near(zoom.scale, 1.0f) && near(zoom.x, 0) && near(zoom.y, 0));
    /* fingers that move while they spread drag the page along */
    yozora_view_zoom_pinch(&start, 160, 240, 2.0f, 200, 240, 320, 480, &zoom, &scroll);
    CHECK(near(zoom.x, 60));
    /* zooming out of a view that was moved down in the viewport: the part of the offset that cannot be held goes to the page */
    zoom.scale = 2.0f;
    zoom.x = 0;
    zoom.y = 240;
    yozora_view_zoom_pinch(&zoom, 160, 0, 0.5f, 160, 0, 320, 480, &start, &scroll);
    CHECK(near(start.scale, 1.0f) && near(start.x, 0) && near(start.y, 0) && near(scroll, 240));
    /* and the other way: fingers low in a view that sits at the top of the viewport, zooming out asks the page to scroll up */
    zoom.y = 0;
    yozora_view_zoom_pinch(&zoom, 160, 480, 0.5f, 160, 480, 320, 480, &start, &scroll);
    CHECK(near(start.scale, 1.0f) && near(start.y, 0) && near(scroll, -240));

    /* a drag moves the view over the viewport and returns what did not fit */
    zoom.scale = 2.0f;
    zoom.x = 0;
    zoom.y = 0;
    yozora_view_zoom_pan(&zoom, 100, 100, 320, 480, &rest_x, &rest_y);
    CHECK(near(zoom.x, 50) && near(zoom.y, 50) && near(rest_x, 0) && near(rest_y, 0));
    yozora_view_zoom_pan(&zoom, 1000, 1000, 320, 480, &rest_x, &rest_y);
    CHECK(near(zoom.x, 160) && near(zoom.y, 240) && near(rest_x, 390) && near(rest_y, 310));
    yozora_view_zoom_pan(&zoom, -2000, -2000, 320, 480, &rest_x, &rest_y);
    CHECK(near(zoom.x, 0) && near(zoom.y, 0) && near(rest_x, -840) && near(rest_y, -760));
    /* without a zoom the viewport has no room and the drag goes to the page whole */
    yozora_view_zoom_reset(&zoom);
    yozora_view_zoom_pan(&zoom, 7, -13, 320, 480, &rest_x, &rest_y);
    CHECK(near(zoom.x, 0) && near(zoom.y, 0) && near(rest_x, 7) && near(rest_y, -13));
    /* a scale that was damaged is read as 1 */
    zoom.scale = NAN;
    yozora_view_zoom_pan(&zoom, 5, 5, 320, 480, &rest_x, &rest_y);
    CHECK(near(rest_x, 5) && near(rest_y, 5));

    /* tiles are painted again in steps of a half, up to the limit of the device */
    CHECK(near(yozora_view_zoom_raster_factor(1.0f, 2.0f), 1.0f));
    CHECK(near(yozora_view_zoom_raster_factor(1.01f, 2.0f), 1.5f));
    CHECK(near(yozora_view_zoom_raster_factor(1.5f, 2.0f), 1.5f));
    CHECK(near(yozora_view_zoom_raster_factor(2.6f, 2.0f), 2.0f));
    CHECK(near(yozora_view_zoom_raster_factor(3.0f, 1.0f), 1.0f));
    CHECK(near(yozora_view_zoom_raster_factor(NAN, 2.0f), 1.0f));
    CHECK(near(yozora_view_zoom_raster_factor(2.0f, NAN), 1.0f));
    return failures ? 1 : 0;
}
