#include "yozora_color.h"
#include <math.h>

static float hold(float value, float low, float high)
{
    if (!(value >= low))
        return low;
    return value > high ? high : value;
}

static uint8_t to_byte(float unit)
{
    return (uint8_t)(hold(unit, 0, 1) * 255.0f + 0.5f);
}

void yozora_color_hsv_to_rgb(float hue, float saturation, float value, uint8_t rgb[3])
{
    hue = hold(hue, 0, 360);
    saturation = hold(saturation, 0, 1);
    value = hold(value, 0, 1);
    if (hue >= 360)
        hue = 0;
    float sector = hue / 60.0f;
    int index = (int)sector;
    float fraction = sector - (float)index;
    float p = value * (1 - saturation);
    float q = value * (1 - saturation * fraction);
    float t = value * (1 - saturation * (1 - fraction));
    float r, g, b;
    switch (index) {
    case 0: r = value; g = t; b = p; break;
    case 1: r = q; g = value; b = p; break;
    case 2: r = p; g = value; b = t; break;
    case 3: r = p; g = q; b = value; break;
    case 4: r = t; g = p; b = value; break;
    default: r = value; g = p; b = q; break;
    }
    rgb[0] = to_byte(r);
    rgb[1] = to_byte(g);
    rgb[2] = to_byte(b);
}

void yozora_color_rgb_to_hsv(const uint8_t rgb[3], float *hue, float *saturation, float *value)
{
    float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;
    float high = fmaxf(r, fmaxf(g, b)), low = fminf(r, fminf(g, b));
    float delta = high - low;
    if (value)
        *value = high;
    if (saturation)
        *saturation = high > 0 ? delta / high : 0;
    if (!hue || delta <= 0)
        return;
    float h;
    if (high == r)
        h = fmodf((g - b) / delta, 6.0f);
    else if (high == g)
        h = (b - r) / delta + 2;
    else
        h = (r - g) / delta + 4;
    h *= 60.0f;
    *hue = h < 0 ? h + 360 : h;
}

static int digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int yozora_color_parse_hex(const char *text, size_t length, uint8_t rgb[3])
{
    if (!text || !rgb)
        return 0;
    if (length && text[0] == '#') {
        ++text;
        --length;
    }
    if (length != 3 && length != 6)
        return 0;
    int values[6];
    for (size_t i = 0; i < length; ++i) {
        values[i] = digit(text[i]);
        if (values[i] < 0)
            return 0;
    }
    for (int i = 0; i < 3; ++i)
        rgb[i] = length == 3 ? (uint8_t)(values[i] * 17) : (uint8_t)(values[2 * i] * 16 + values[2 * i + 1]);
    return 1;
}

void yozora_color_format_hex(const uint8_t rgb[3], char out[8])
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '#';
    for (int i = 0; i < 3; ++i) {
        out[1 + 2 * i] = digits[rgb[i] >> 4];
        out[2 + 2 * i] = digits[rgb[i] & 15];
    }
    out[7] = 0;
}
