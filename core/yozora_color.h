#ifndef YOZORA_COLOR_H
#define YOZORA_COLOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a color field's colors: red, green, blue in bytes, hue in degrees 0 to 360, saturation and value 0 to 1 */

/* the hue is held to 0..360 (360 is red again), the saturation and the value to 0..1, a value that is not a number counts as 0 */
void yozora_color_hsv_to_rgb(float hue, float saturation, float value, uint8_t rgb[3]);
/* a grey has no hue: *hue is left as it was, so a picker that moves the saturation to 0 and back keeps its place on the hue track */
void yozora_color_rgb_to_hsv(const uint8_t rgb[3], float *hue, float *saturation, float *value);
/* "#rrggbb", "rrggbb", "#rgb" or "rgb" in either case, 1 when it is a color. nothing is written when it is not */
int yozora_color_parse_hex(const char *text, size_t length, uint8_t rgb[3]);
/* "#RRGGBB" and a terminator */
void yozora_color_format_hex(const uint8_t rgb[3], char out[8]);

#ifdef __cplusplus
}
#endif
#endif
