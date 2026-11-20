#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "yozora_accent.h"

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void parsing(void) {
    yozora_accent a = { 77, 0.5f, 0.5f };
    char hex[7];
    assert(yozora_accent_parse_hex("A44BBE", 6, &a));
    assert(near(a.hue, 286.4f, 0.2f) && near(a.saturation, 0.605f, 0.005f) && near(a.brightness, 1, 0.001f));
    yozora_accent_hex(&a, hex);
    assert(!strcmp(hex, "A44BBE"));
    /* three digits are the short form of six */
    assert(yozora_accent_parse_hex("f00", 3, &a));
    yozora_accent_hex(&a, hex);
    assert(near(a.hue, 0, 0.01f) && near(a.saturation, 1, 0.001f));
    assert(!strcmp(hex, "BE0000"));
    /* a grey and a black keep the hue of the person, a color brighter than the accent counts as full brightness */
    a.hue = 123;
    assert(yozora_accent_parse_hex("808080", 6, &a));
    assert(a.hue == 123 && a.saturation == 0);
    assert(yozora_accent_parse_hex("000000", 6, &a));
    assert(a.hue == 123 && a.brightness == 0);
    assert(yozora_accent_parse_hex("FFFFFF", 6, &a));
    assert(a.brightness == 1);
    /* a rejected text leaves the accent as it was */
    a.hue = 10; a.saturation = 0.2f; a.brightness = 0.3f;
    assert(!yozora_accent_parse_hex("12345", 5, &a));
    assert(!yozora_accent_parse_hex("12345G", 6, &a));
    assert(!yozora_accent_parse_hex("", 0, &a));
    assert(!yozora_accent_parse_hex("1234567", 7, &a));
    assert(!yozora_accent_parse_hex(NULL, 6, &a));
    assert(!yozora_accent_parse_hex("ffffff", 6, NULL));
    assert(a.hue == 10 && a.saturation == 0.2f && a.brightness == 0.3f);
}

static void turning(void) {
    yozora_accent palette = { YOZORA_ACCENT_PALETTE_HUE, YOZORA_ACCENT_PALETTE_SATURATION, 1 };
    yozora_accent green = { 106.0f, 0.6f, 1 };
    float r = 0xa4 / 255.0f, g = 0x4b / 255.0f, b = 0xbe / 255.0f;
    float r0 = r, g0 = g, b0 = b;
    /* the palette accent turned to itself is the same color */
    yozora_accent_turn(&palette, 1, &r, &g, &b);
    assert(near(r, r0, 0.01f) && near(g, g0, 0.01f) && near(b, b0, 0.01f));
    /* a hue 180 degrees away turns the purple to green, value and saturation stay */
    r = r0; g = g0; b = b0;
    yozora_accent_turn(&green, 1, &r, &g, &b);
    assert(g > r && g > b && near(fmaxf(r, fmaxf(g, b)), b0, 0.01f));
    /* a grey stays a grey whatever the accent */
    r = g = b = 0.4f;
    yozora_accent_turn(&green, 1, &r, &g, &b);
    assert(r == g && g == b && near(r, 0.4f, 0.0001f));
    /* a grey accent takes the color out of every surface */
    {
        yozora_accent grey = { 0, 0, 1 };
        r = r0; g = g0; b = b0;
        yozora_accent_turn(&grey, 1, &r, &g, &b);
        assert(near(r, g, 0.0001f) && near(g, b, 0.0001f));
    }
    /* brightness dims a dark surface by its share and a light one down to six tenths, text keeps its value */
    {
        yozora_accent dark = { 286, 0.6f, 0 };
        r = g = b = 0.5f;
        yozora_accent_turn(&dark, 1, &r, &g, &b);
        assert(near(r, 0, 0.0001f));
        r = g = b = 1;
        yozora_accent_turn(&dark, 1, &r, &g, &b);
        assert(near(r, 0.6f, 0.0001f));
        r = g = b = 1;
        yozora_accent_turn(&dark, 0, &r, &g, &b);
        assert(near(r, 1, 0.0001f));
    }
    /* nan and infinity do not leak into a color */
    {
        yozora_accent wild = { NAN, INFINITY, -1 };
        r = r0; g = g0; b = b0;
        yozora_accent_turn(&wild, 1, &r, &g, &b);
        assert(r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1);
    }
}

static float hue_of(float r, float g, float b) {
    float high = fmaxf(r, fmaxf(g, b)), low = fminf(r, fminf(g, b)), span = high - low, turn;
    if (span <= 0) return 0;
    if (high == r) turn = fmodf((g - b) / span, 6);
    else if (high == g) turn = (b - r) / span + 2;
    else turn = (r - g) / span + 4;
    turn *= 60;
    return turn < 0 ? turn + 360 : turn;
}

static float hue_distance(float a, float b) {
    float d = fabsf(fmodf(a - b, 360));
    return d > 180 ? 360 - d : d;
}

/* a palette color takes the hue of the accent: the grounds sit 31 degrees behind the light accent and that
   distance must not follow a far accent (a red accent once turned them magenta). near the purple the distance
   stays, so the palette is not changed by its own accent */
static void hues(void) {
    static const struct { float hue; } accents[] = { { 0 }, { 30 }, { 60 }, { 120 }, { 180 }, { 220 }, { 337 } };
    unsigned i;
    float r, g, b;
    /* the ground of the dark theme, 0x15121f, hue 255 */
    for (i = 0; i < sizeof accents / sizeof accents[0]; ++i) {
        yozora_accent accent = { accents[i].hue, 0.8f, 1 };
        r = 0x15 / 255.0f; g = 0x12 / 255.0f; b = 0x1f / 255.0f;
        yozora_accent_turn(&accent, 1, &r, &g, &b);
        assert(hue_distance(hue_of(r, g, b), accents[i].hue) < 10.0f);
        /* the light accent of the dark theme, 0xac62de */
        r = 0xac / 255.0f; g = 0x62 / 255.0f; b = 0xde / 255.0f;
        yozora_accent_turn(&accent, 0, &r, &g, &b);
        assert(hue_distance(hue_of(r, g, b), accents[i].hue) < 10.0f);
    }
    /* the purple of the palette itself changes nothing */
    {
        yozora_accent same = { YOZORA_ACCENT_PALETTE_HUE, YOZORA_ACCENT_PALETTE_SATURATION, 1 };
        r = 0x15 / 255.0f; g = 0x12 / 255.0f; b = 0x1f / 255.0f;
        yozora_accent_turn(&same, 1, &r, &g, &b);
        assert(near(r, 0x15 / 255.0f, 0.004f) && near(g, 0x12 / 255.0f, 0.004f) && near(b, 0x1f / 255.0f, 0.004f));
    }
    /* a little way off the purple the grounds move a little, with no jump: 276 degrees against 286 moves them by about the distance */
    {
        yozora_accent near_purple = { 276, YOZORA_ACCENT_PALETTE_SATURATION, 1 };
        float h_near;
        r = 0x15 / 255.0f; g = 0x12 / 255.0f; b = 0x1f / 255.0f;
        yozora_accent_turn(&near_purple, 1, &r, &g, &b);
        h_near = hue_of(r, g, b);
        assert(hue_distance(h_near, 255) < 15.0f);
    }
}

/* the private palette is measured from its own hue: an accent at that hue and the palette saturation turns
   nothing, others take their own hue */
static void privateturning(void) {
    float r, g, b;
    yozora_accent same = { YOZORA_PRIVATE_PALETTE_HUE, YOZORA_ACCENT_PALETTE_SATURATION, 1 };
    /* the deep purple of the private ground, 0x2b1048 */
    float r0 = 0x2b / 255.0f, g0 = 0x10 / 255.0f, b0 = 0x48 / 255.0f;
    yozora_accent red = { 0, 0.9f, 1 }, green = { 130, 0.9f, 1 };
    r = r0; g = g0; b = b0;
    yozora_accent_turn_about(&same, YOZORA_PRIVATE_PALETTE_HUE, 0, &r, &g, &b);
    assert(near(r, r0, 0.004f) && near(g, g0, 0.004f) && near(b, b0, 0.004f));
    r = r0; g = g0; b = b0;
    yozora_accent_turn_about(&red, YOZORA_PRIVATE_PALETTE_HUE, 1, &r, &g, &b);
    assert(hue_distance(hue_of(r, g, b), 0) < 10.0f);
    r = r0; g = g0; b = b0;
    yozora_accent_turn_about(&green, YOZORA_PRIVATE_PALETTE_HUE, 1, &r, &g, &b);
    assert(hue_distance(hue_of(r, g, b), 130) < 10.0f);
    /* a grey and a black have no hue to take: they stay grey */
    {
        yozora_accent black = { 286, 0, 0 };
        r = r0; g = g0; b = b0;
        yozora_accent_turn_about(&black, YOZORA_PRIVATE_PALETTE_HUE, 1, &r, &g, &b);
        assert(near(r, g, 0.004f) && near(g, b, 0.004f));
    }
}

int main(void) {
    parsing();
    turning();
    hues();
    privateturning();
    puts("accent: hex parsing, rejection, the turn of the palette, grey, brightness and bounds passed");
    return 0;
}
