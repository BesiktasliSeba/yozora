#include "yozora_blur.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_blur: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static uint32_t total(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride, uint32_t channel)
{
    uint32_t sum = 0;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            sum += pixels[y * stride + x * 4u + channel];
    return sum;
}

int main(void)
{
    /* the radius is 5 percent of the long side at 100, held to the range, and never past the cap */
    CHECK(yozora_blur_radius(0, 960) == 0);
    CHECK(yozora_blur_radius(100, 960) == 48);
    CHECK(yozora_blur_radius(50, 960) == 24);
    CHECK(yozora_blur_radius(-20, 960) == 0);
    CHECK(yozora_blur_radius(400, 960) == 48);
    CHECK(yozora_blur_radius(100, 0xffffffffu) == 4096);
    CHECK(yozora_blur_radius(100, 0) == 0);

    /* the shrink keeps the box at 8 pixels or less, and a blur that was asked for survives it */
    CHECK(yozora_blur_shrink(0) == 1);
    CHECK(yozora_blur_shrink(8) == 1);
    CHECK(yozora_blur_shrink(9) == 2);
    CHECK(yozora_blur_shrink(48) == 6);
    CHECK(yozora_blur_work_radius(0, 4) == 0);
    CHECK(yozora_blur_work_radius(1, 6) == 1);
    CHECK(yozora_blur_work_radius(48, 6) == 8);
    CHECK(yozora_blur_work_radius(5, 0) == 5);

    CHECK(yozora_blur_scratch_size(0, 5) == 0);
    CHECK(yozora_blur_scratch_size(5, 0) == 0);
    CHECK(yozora_blur_scratch_size(10, 20) == 80);

    /* bad input is refused, a missing or short scratch is told apart */
    uint8_t px[8 * 8 * 4];
    uint8_t scratch[8 * 4];
    memset(px, 0, sizeof px);
    CHECK(yozora_blur_rgba(NULL, 8, 8, 32, 1, scratch, sizeof scratch) == YOZORA_BLUR_INVALID);
    CHECK(yozora_blur_rgba(px, 0, 8, 32, 1, scratch, sizeof scratch) == YOZORA_BLUR_INVALID);
    CHECK(yozora_blur_rgba(px, 8, 0, 32, 1, scratch, sizeof scratch) == YOZORA_BLUR_INVALID);
    CHECK(yozora_blur_rgba(px, 8, 8, 31, 1, scratch, sizeof scratch) == YOZORA_BLUR_INVALID);
    CHECK(yozora_blur_rgba(px, 8, 8, 32, 5000, scratch, sizeof scratch) == YOZORA_BLUR_INVALID);
    CHECK(yozora_blur_rgba(px, 8, 8, 32, 1, NULL, 0) == YOZORA_BLUR_SCRATCH);
    CHECK(yozora_blur_rgba(px, 8, 8, 32, 1, scratch, sizeof scratch - 1) == YOZORA_BLUR_SCRATCH);

    /* a radius of 0 does not touch the pixels, and needs no scratch */
    for (size_t i = 0; i < sizeof px; ++i)
        px[i] = (uint8_t)(i * 7u);
    uint8_t copy[sizeof px];
    memcpy(copy, px, sizeof px);
    CHECK(yozora_blur_rgba(px, 8, 8, 32, 0, NULL, 0) == YOZORA_BLUR_OK);
    CHECK(memcmp(px, copy, sizeof px) == 0);

    /* a flat picture stays flat whatever the radius, the edges repeat and do not darken */
    memset(px, 200, sizeof px);
    CHECK(yozora_blur_rgba(px, 8, 8, 32, 3, scratch, sizeof scratch) == YOZORA_BLUR_OK);
    for (size_t i = 0; i < sizeof px; ++i)
        CHECK(px[i] == 200);

    /* a point of light spreads and keeps its weight up to the rounding, the neighbours of the point get some of it, symmetric about it */
    uint8_t big[15 * 15 * 4];
    uint8_t line[15 * 4];
    memset(big, 0, sizeof big);
    big[(7 * 15 + 7) * 4 + 1] = 255;
    CHECK(yozora_blur_rgba(big, 15, 15, 60, 1, line, sizeof line) == YOZORA_BLUR_OK);
    CHECK(big[(7 * 15 + 7) * 4 + 1] > 0 && big[(7 * 15 + 7) * 4 + 1] < 255);
    CHECK(big[(7 * 15 + 6) * 4 + 1] == big[(7 * 15 + 8) * 4 + 1]);
    CHECK(big[(6 * 15 + 7) * 4 + 1] == big[(8 * 15 + 7) * 4 + 1]);
    CHECK(big[(7 * 15 + 6) * 4 + 1] == big[(6 * 15 + 7) * 4 + 1]);
    CHECK(big[(7 * 15 + 6) * 4 + 1] > 0);
    CHECK(big[(7 * 15 + 12) * 4 + 1] == 0);
    CHECK(total(big, 15, 15, 60, 0) == 0);
    CHECK(total(big, 15, 15, 60, 1) > 255 * 3 / 4 && total(big, 15, 15, 60, 1) < 255 + 255 / 4);

    /* a row stride wider than the pixels leaves the padding alone */
    uint8_t padded[4 * 4 * 4 + 4 * 4];
    uint8_t small_line[4 * 4];
    memset(padded, 0xab, sizeof padded);
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 16; ++x)
            padded[y * 20 + x] = (uint8_t)(x + y * 10);
    CHECK(yozora_blur_rgba(padded, 4, 4, 20, 1, small_line, sizeof small_line) == YOZORA_BLUR_OK);
    for (uint32_t y = 0; y < 3; ++y)
        for (uint32_t x = 16; x < 20; ++x)
            CHECK(padded[y * 20 + x] == 0xab);

    /* a picture of one pixel is its own blur, and a one pixel wide column blurs along its length only */
    uint8_t one[4] = { 10, 20, 30, 255 };
    uint8_t one_line[4];
    CHECK(yozora_blur_rgba(one, 1, 1, 4, 5, one_line, sizeof one_line) == YOZORA_BLUR_OK);
    CHECK(one[0] == 10 && one[1] == 20 && one[2] == 30 && one[3] == 255);

    /* the lightness of a picture: black, white, the weights of the channels, alpha unread, a stride wider than a row, and empty input */
    uint8_t black[4 * 4 * 4], white[4 * 4 * 4], mixed[2 * 4];
    memset(black, 0, sizeof black);
    memset(white, 255, sizeof white);
    CHECK(yozora_luma_average(black, 4, 4, 16) == 0);
    CHECK(yozora_luma_average(white, 4, 4, 16) == 255);
    mixed[0] = 255; mixed[1] = 0; mixed[2] = 0; mixed[3] = 0;
    mixed[4] = 0; mixed[5] = 255; mixed[6] = 0; mixed[7] = 255;
    CHECK(yozora_luma_average(mixed, 1, 1, 4) == 76);
    CHECK(yozora_luma_average(mixed + 4, 1, 1, 4) == 149);
    CHECK(yozora_luma_average(mixed, 2, 1, 8) == 113);
    uint8_t wide[2 * 12];
    memset(wide, 255, sizeof wide);
    memset(wide + 8, 0, 4);
    memset(wide + 20, 0, 4);
    CHECK(yozora_luma_average(wide, 2, 2, 12) == 255);
    CHECK(yozora_luma_average(wide, 0, 2, 12) == 0);
    CHECK(yozora_luma_average(wide, 2, 0, 12) == 0);
    CHECK(yozora_luma_average(wide, 2, 2, 7) == 0);
    CHECK(yozora_luma_average(NULL, 2, 2, 12) == 0);

    if (failures) {
        fprintf(stderr, "test_blur: %d failures\n", failures);
        return 1;
    }
    printf("test_blur: ok\n");
    return 0;
}
