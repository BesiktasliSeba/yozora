#include "yozora_accent.h"

#include <math.h>

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static float clamp01(float v) {
    if (!(v > 0)) return 0;
    return v > 1 ? 1 : v;
}

static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v) {
    float high = fmaxf(r, fmaxf(g, b)), low = fminf(r, fminf(g, b)), span = high - low, turn = 0;
    if (span > 0) {
        if (high == r) turn = fmodf((g - b) / span, 6);
        else if (high == g) turn = (b - r) / span + 2;
        else turn = (r - g) / span + 4;
        turn *= 60;
        if (turn < 0) turn += 360;
    }
    *h = turn;
    *s = high > 0 ? span / high : 0;
    *v = high;
}

static void hsv_to_rgb(float hue, float saturation, float value, float *red, float *green, float *blue) {
    float sector, chroma, x, match, r = 0, g = 0, b = 0;
    hue = isfinite(hue) ? fmodf(hue, 360) : 0;
    if (hue < 0) hue += 360;
    saturation = clamp01(saturation);
    value = clamp01(value);
    sector = hue / 60;
    chroma = value * saturation;
    x = chroma * (1 - fabsf(fmodf(sector, 2) - 1));
    if (sector < 1) { r = chroma; g = x; }
    else if (sector < 2) { r = x; g = chroma; }
    else if (sector < 3) { g = chroma; b = x; }
    else if (sector < 4) { g = x; b = chroma; }
    else if (sector < 5) { r = x; b = chroma; }
    else { r = chroma; b = x; }
    match = value - chroma;
    *red = r + match;
    *green = g + match;
    *blue = b + match;
}

int yozora_accent_parse_hex(const char *text, size_t length, yozora_accent *accent) {
    unsigned rgb = 0;
    size_t i;
    float h, s, v;
    if (!text || !accent || (length != 3 && length != 6)) return 0;
    for (i = 0; i < length; ++i) {
        int digit = hex_digit(text[i]);
        if (digit < 0) return 0;
        rgb = (rgb << 4) | (unsigned)digit;
        if (length == 3) rgb = (rgb << 4) | (unsigned)digit;
    }
    rgb_to_hsv(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, &h, &s, &v);
    if (s > 0 && v > 0) accent->hue = h;
    accent->saturation = s;
    accent->brightness = fminf(1, v / YOZORA_ACCENT_VALUE);
    return 1;
}

void yozora_accent_rgb(const yozora_accent *accent, float *red, float *green, float *blue) {
    hsv_to_rgb(accent->hue, accent->saturation, YOZORA_ACCENT_VALUE * clamp01(accent->brightness), red, green, blue);
}

void yozora_accent_hex(const yozora_accent *accent, char out[7]) {
    static const char digits[] = "0123456789ABCDEF";
    float c[3];
    int i;
    yozora_accent_rgb(accent, &c[0], &c[1], &c[2]);
    for (i = 0; i < 3; ++i) {
        unsigned byte = (unsigned)(clamp01(c[i]) * 255 + 0.5f);
        out[2 * i] = digits[byte >> 4];
        out[2 * i + 1] = digits[byte & 15];
    }
    out[6] = '\0';
}

static float wrap_signed(float degrees) {
    degrees = fmodf(degrees, 360);
    if (degrees > 180) degrees -= 360;
    if (degrees < -180) degrees += 360;
    return degrees;
}

void yozora_accent_turn_about(const yozora_accent *accent, float palette_hue, int dim, float *red, float *green, float *blue) {
    float h, s, v, brightness = clamp01(accent->brightness);
    float scale = accent->saturation / YOZORA_ACCENT_PALETTE_SATURATION;
    rgb_to_hsv(*red, *green, *blue, &h, &s, &v);
    if (s > 0) {
        float keep = 1 - fabsf(wrap_signed(accent->hue - palette_hue)) / 60;
        if (keep < 0) keep = 0;
        h = accent->hue + wrap_signed(h - palette_hue) * keep;
        s = fminf(1, s * scale);
    }
    if (dim && brightness < 1)
        v *= v > 0.7f ? 0.6f + 0.4f * brightness : brightness;
    hsv_to_rgb(h, s, v, red, green, blue);
}

void yozora_accent_turn(const yozora_accent *accent, int dim, float *red, float *green, float *blue) {
    yozora_accent_turn_about(accent, YOZORA_ACCENT_PALETTE_HUE, dim, red, green, blue);
}
