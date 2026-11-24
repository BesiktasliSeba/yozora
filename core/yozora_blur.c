#include "yozora_blur.h"

#define BLUR_MAX_RADIUS 4096u
#define BLUR_PASSES 3

uint32_t yozora_blur_radius(int percent, uint32_t long_side)
{
    if (percent < 0)
        percent = 0;
    if (percent > YOZORA_BLUR_MAX_PERCENT)
        percent = YOZORA_BLUR_MAX_PERCENT;
    uint64_t radius = ((uint64_t)long_side * (uint64_t)percent * 5u + 5000u) / 10000u;
    return radius > BLUR_MAX_RADIUS ? BLUR_MAX_RADIUS : (uint32_t)radius;
}

uint32_t yozora_blur_shrink(uint32_t radius)
{
    uint32_t shrink = (radius + YOZORA_BLUR_WORK_RADIUS - 1) / YOZORA_BLUR_WORK_RADIUS;
    return shrink ? shrink : 1;
}

uint32_t yozora_blur_work_radius(uint32_t radius, uint32_t shrink)
{
    if (!radius)
        return 0;
    if (shrink < 1)
        shrink = 1;
    uint32_t work = (radius + shrink / 2) / shrink;
    return work ? work : 1;
}

size_t yozora_blur_scratch_size(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return 0;
    return (size_t)(width > height ? width : height) * 4u;
}

/* one line of n pixels with 4 channels, read from line and written to out. the window is 2r + 1 wide and the first and last pixel repeat past the ends */
static void blur_line(const uint8_t *line, uint8_t *out, size_t out_step, uint32_t n, uint32_t r)
{
    uint32_t window = 2u * r + 1u;
    for (uint32_t channel = 0; channel < 4; ++channel) {
        uint32_t sum = (r + 1u) * line[channel];
        for (uint32_t k = 1; k <= r; ++k)
            sum += line[(size_t)(k < n ? k : n - 1u) * 4u + channel];
        for (uint32_t i = 0; i < n; ++i) {
            out[(size_t)i * out_step + channel] = (uint8_t)((sum + window / 2u) / window);
            uint32_t add = i + r + 1u;
            uint32_t drop = i > r ? i - r : 0;
            sum += line[(size_t)(add < n ? add : n - 1u) * 4u + channel];
            sum -= line[(size_t)drop * 4u + channel];
        }
    }
}

uint32_t yozora_luma_average(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride)
{
    if (!pixels || !width || !height || stride < (size_t)width * 4u)
        return 0;
    uint64_t sum = 0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = pixels + (size_t)y * stride;
        for (uint32_t x = 0; x < width; ++x)
            sum += (77u * row[(size_t)x * 4u] + 150u * row[(size_t)x * 4u + 1] + 29u * row[(size_t)x * 4u + 2]) >> 8;
    }
    uint64_t count = (uint64_t)width * height;
    return (uint32_t)((sum + count / 2) / count);
}

yozora_blur_status yozora_blur_rgba(uint8_t *pixels, uint32_t width, uint32_t height, size_t stride, uint32_t radius, uint8_t *scratch,
                                    size_t scratch_capacity)
{
    if (!pixels || !width || !height || stride < (size_t)width * 4u || radius > BLUR_MAX_RADIUS)
        return YOZORA_BLUR_INVALID;
    if (!radius)
        return YOZORA_BLUR_OK;
    if (!scratch || scratch_capacity < yozora_blur_scratch_size(width, height))
        return YOZORA_BLUR_SCRATCH;
    for (int pass = 0; pass < BLUR_PASSES; ++pass) {
        for (uint32_t y = 0; y < height; ++y) {
            uint8_t *row = pixels + (size_t)y * stride;
            for (size_t i = 0; i < (size_t)width * 4u; ++i)
                scratch[i] = row[i];
            blur_line(scratch, row, 4u, width, radius);
        }
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t *column = pixels + (size_t)x * 4u;
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t channel = 0; channel < 4; ++channel)
                    scratch[(size_t)y * 4u + channel] = column[(size_t)y * stride + channel];
            blur_line(scratch, column, stride, height, radius);
        }
    }
    return YOZORA_BLUR_OK;
}
