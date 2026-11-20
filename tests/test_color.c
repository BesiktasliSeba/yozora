#include "yozora_color.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_color: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static int rgb_is(float h, float s, float v, int r, int g, int b)
{
    uint8_t rgb[3];
    yozora_color_hsv_to_rgb(h, s, v, rgb);
    return rgb[0] == r && rgb[1] == g && rgb[2] == b;
}

int main(void)
{
    /* the primaries, the secondaries and the greys */
    CHECK(rgb_is(0, 1, 1, 255, 0, 0));
    CHECK(rgb_is(60, 1, 1, 255, 255, 0));
    CHECK(rgb_is(120, 1, 1, 0, 255, 0));
    CHECK(rgb_is(180, 1, 1, 0, 255, 255));
    CHECK(rgb_is(240, 1, 1, 0, 0, 255));
    CHECK(rgb_is(300, 1, 1, 255, 0, 255));
    CHECK(rgb_is(360, 1, 1, 255, 0, 0));
    CHECK(rgb_is(200, 0, 1, 255, 255, 255));
    CHECK(rgb_is(200, 0.5f, 0, 0, 0, 0));
    CHECK(rgb_is(0, 0, 0.5f, 128, 128, 128));
    /* out of range and broken input is held */
    CHECK(rgb_is(-30, 1, 1, 255, 0, 0));
    CHECK(rgb_is(720, 1, 1, 255, 0, 0));
    CHECK(rgb_is(0, 7, 1, 255, 0, 0));
    CHECK(rgb_is(0, 1, NAN, 0, 0, 0));
    CHECK(rgb_is(NAN, 1, 1, 255, 0, 0));

    /* a round trip through the hue, the saturation and the value keeps every byte for these colors */
    const uint8_t samples[][3] = { { 255, 0, 0 }, { 12, 200, 99 }, { 164, 75, 190 }, { 1, 2, 3 }, { 255, 255, 255 }, { 0, 0, 0 }, { 128, 128, 128 }, { 250, 128, 114 } };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        float h = 17, s, v;
        uint8_t back[3];
        yozora_color_rgb_to_hsv(samples[i], &h, &s, &v);
        yozora_color_hsv_to_rgb(h, s, v, back);
        CHECK(memcmp(back, samples[i], 3) == 0);
    }
    /* a grey leaves the hue where it was, a black and a white too */
    float h = 123, s = 9, v = 9;
    const uint8_t grey[3] = { 90, 90, 90 };
    yozora_color_rgb_to_hsv(grey, &h, &s, &v);
    CHECK(h == 123 && s == 0 && fabsf(v - 90 / 255.0f) < 1e-6f);
    const uint8_t black[3] = { 0, 0, 0 };
    yozora_color_rgb_to_hsv(black, &h, &s, &v);
    CHECK(h == 123 && s == 0 && v == 0);
    const uint8_t blue[3] = { 0, 0, 255 };
    yozora_color_rgb_to_hsv(blue, &h, &s, &v);
    CHECK(h == 240 && s == 1 && v == 1);
    /* a color between magenta and red has a hue below 360 (329.9 here) */
    const uint8_t rose[3] = { 255, 0, 128 };
    yozora_color_rgb_to_hsv(rose, &h, &s, &v);
    CHECK(h > 329.5f && h < 330.5f);
    /* null outputs are skipped */
    yozora_color_rgb_to_hsv(rose, NULL, NULL, NULL);

    uint8_t rgb[3] = { 9, 9, 9 };
    CHECK(yozora_color_parse_hex("#a44bbe", 7, rgb) && rgb[0] == 0xa4 && rgb[1] == 0x4b && rgb[2] == 0xbe);
    CHECK(yozora_color_parse_hex("A44BBE", 6, rgb) && rgb[0] == 0xa4);
    CHECK(yozora_color_parse_hex("#fc0", 4, rgb) && rgb[0] == 0xff && rgb[1] == 0xcc && rgb[2] == 0);
    CHECK(yozora_color_parse_hex("fc0", 3, rgb) && rgb[1] == 0xcc);
    uint8_t keep[3] = { 1, 2, 3 };
    CHECK(!yozora_color_parse_hex("", 0, keep) && keep[0] == 1);
    CHECK(!yozora_color_parse_hex("#", 1, keep));
    CHECK(!yozora_color_parse_hex("12345", 5, keep));
    CHECK(!yozora_color_parse_hex("1234567", 7, keep));
    CHECK(!yozora_color_parse_hex("#12345g", 7, keep));
    CHECK(!yozora_color_parse_hex("12 456", 6, keep));
    CHECK(!yozora_color_parse_hex(NULL, 6, keep));
    CHECK(keep[0] == 1 && keep[1] == 2 && keep[2] == 3);

    char text[8];
    const uint8_t sample[3] = { 0xa4, 0x4b, 0x0e };
    yozora_color_format_hex(sample, text);
    CHECK(strcmp(text, "#A44B0E") == 0);
    const uint8_t zero[3] = { 0, 0, 0 };
    yozora_color_format_hex(zero, text);
    CHECK(strcmp(text, "#000000") == 0);

    if (failures)
        return 1;
    puts("test_color: ok");
    return 0;
}
