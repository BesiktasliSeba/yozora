#ifndef YOZORA_ACCENT_H
#define YOZORA_ACCENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* hue and saturation of the palette's light accent (0xA44BBE), which a custom accent is measured from, and
   its value */
#define YOZORA_ACCENT_PALETTE_HUE 286.0f
#define YOZORA_ACCENT_PALETTE_SATURATION 0.6f
#define YOZORA_ACCENT_VALUE 0.745f

/* a custom color: hue in degrees 0 to 360, saturation and brightness 0 to 1. brightness 1 is the value of the
   light accent, 0 is black, the palette keeps that share of the value of its surfaces */
typedef struct yozora_accent {
    float hue;
    float saturation;
    float brightness;
} yozora_accent;

/* three or six hex digits, no #. a grey or black has no hue so *hue stays as it was. 0 when the text is no
   color, nothing is written then */
int yozora_accent_parse_hex(const char *text, size_t length, yozora_accent *accent);
/* the accent as six uppercase hex digits and a terminator */
void yozora_accent_hex(const yozora_accent *accent, char out[7]);
/* hue of the private palette's deep purple (0x2b1048), the private accent is measured from it as the normal
   one is from YOZORA_ACCENT_PALETTE_HUE */
#define YOZORA_PRIVATE_PALETTE_HUE 269.0f
/* the accent itself, as the light accent of the palette is drawn */
void yozora_accent_rgb(const yozora_accent *accent, float *red, float *green, float *blue);
/* a palette color turned to the accent: hue moves by the accent's distance from the palette hue, saturation
   follows and greys stay grey. with dim set brightness is taken too, a light color is dimmed to six tenths so
   dark words stay readable. text colors pass 0 and keep their value */
void yozora_accent_turn(const yozora_accent *accent, int dim, float *red, float *green, float *blue);
/* the same for a palette with its own purple hue (the private one). a color keeps its small distance from
   that hue only near the purple, fading out over 60 degrees, so a red accent gives red grounds and not the
   magenta a shift of every hue would give, the grounds sit 31 degrees behind the light accent */
void yozora_accent_turn_about(const yozora_accent *accent, float palette_hue, int dim, float *red, float *green, float *blue);

#ifdef __cplusplus
}
#endif
#endif
