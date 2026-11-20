#include "yozora_zoom.h"

#include <math.h>

int yozora_zoom_clamp_fit(int percent)
{
    if (percent < YOZORA_ZOOM_FIT_MIN)
        return YOZORA_ZOOM_FIT_MIN;
    if (percent > YOZORA_ZOOM_MAX)
        return YOZORA_ZOOM_MAX;
    return percent;
}

int yozora_zoom_for_viewport(int view_width, double meta_width)
{
    /* the comparison is false for NaN, so it is rejected with the rest */
    if (view_width <= 0 || !(meta_width > 0) || !(meta_width > view_width))
        return YOZORA_ZOOM_DEFAULT;
    if (meta_width > 10000)
        meta_width = 10000;
    return yozora_zoom_clamp_fit((int)((double)view_width * 100.0 / meta_width));
}

int yozora_zoom_clamp(int percent)
{
    if (percent < YOZORA_ZOOM_MIN)
        return YOZORA_ZOOM_MIN;
    if (percent > YOZORA_ZOOM_MAX)
        return YOZORA_ZOOM_MAX;
    return percent;
}

int yozora_zoom_anchor_scroll(int scroll, int focal, int old_percent, int new_percent, int minimum, int maximum)
{
    if (old_percent < 1)
        old_percent = YOZORA_ZOOM_DEFAULT;
    long long row = (long long)scroll + focal;
    long long moved = (row * new_percent * 2 + old_percent) / (old_percent * 2LL);
    long long result = moved - focal;
    if (maximum < minimum)
        maximum = minimum;
    if (result < minimum)
        result = minimum;
    if (result > maximum)
        result = maximum;
    return (int)result;
}

void yozora_view_zoom_reset(yozora_view_zoom *zoom)
{
    zoom->scale = 1.0f;
    zoom->x = 0;
    zoom->y = 0;
}

float yozora_view_zoom_clamp(float scale)
{
    if (!(scale >= 1.0f))
        return 1.0f;
    if (scale > YOZORA_VIEW_ZOOM_MAX)
        return YOZORA_VIEW_ZOOM_MAX;
    return scale;
}

static float held(float value, float maximum, float *excess)
{
    *excess = 0;
    if (value < 0) {
        *excess = value;
        return 0;
    }
    if (value > maximum) {
        *excess = value - maximum;
        return maximum;
    }
    return value;
}

void yozora_view_zoom_pinch(const yozora_view_zoom *start, float start_x, float start_y, float scale, float focal_x, float focal_y,
    float width, float height, yozora_view_zoom *out, float *scroll_y)
{
    float zoom_scale = yozora_view_zoom_clamp(start->scale * (scale > 0 && isfinite(scale) ? scale : 1.0f));
    /* the viewport point under the fingers when the pinch began */
    float under_x = start->x + start_x / start->scale;
    float under_y = start->y + start_y / start->scale;
    float excess_x, excess_y;
    out->scale = zoom_scale;
    out->x = held(under_x - focal_x / zoom_scale, width - width / zoom_scale, &excess_x);
    out->y = held(under_y - focal_y / zoom_scale, height - height / zoom_scale, &excess_y);
    if (scroll_y)
        *scroll_y = excess_y;
}

void yozora_view_zoom_pan(yozora_view_zoom *zoom, float dx, float dy, float width, float height, float *rest_x, float *rest_y)
{
    float excess_x, excess_y;
    float scale = yozora_view_zoom_clamp(zoom->scale);
    zoom->x = held(zoom->x + dx / scale, width - width / scale, &excess_x);
    zoom->y = held(zoom->y + dy / scale, height - height / scale, &excess_y);
    if (rest_x)
        *rest_x = excess_x;
    if (rest_y)
        *rest_y = excess_y;
}

void yozora_view_zoom_to_viewport(const yozora_view_zoom *zoom, float x, float y, float *out_x, float *out_y)
{
    *out_x = zoom->x + x / zoom->scale;
    *out_y = zoom->y + y / zoom->scale;
}

void yozora_view_zoom_to_screen(const yozora_view_zoom *zoom, float x, float y, float *out_x, float *out_y)
{
    *out_x = (x - zoom->x) * zoom->scale;
    *out_y = (y - zoom->y) * zoom->scale;
}

float yozora_view_zoom_raster_factor(float scale, float max_factor)
{
    float factor = ceilf(yozora_view_zoom_clamp(scale) * 2.0f) / 2.0f;
    if (!(max_factor >= 1.0f))
        max_factor = 1.0f;
    return factor > max_factor ? max_factor : factor;
}
