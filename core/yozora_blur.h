#ifndef YOZORA_BLUR_H
#define YOZORA_BLUR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOZORA_BLUR_OK = 0,
    YOZORA_BLUR_INVALID = 1,
    YOZORA_BLUR_SCRATCH = 2
} yozora_blur_status;

/* the box of one pass reaches this many pixels at most, a larger blur shrinks the picture first */
#define YOZORA_BLUR_WORK_RADIUS 8
#define YOZORA_BLUR_MAX_PERCENT 100

/* the blur radius in pixels of a picture whose long side is long_side, for a setting of 0 to 100 percent (held to that range): 5 percent of the long
   side at 100. three passes of a box of this radius are close to a gaussian of the same sigma */
uint32_t yozora_blur_radius(int percent, uint32_t long_side);

/* the divisor, 1 or more, by which a picture is shrunk before the blur so the box of a pass stays within YOZORA_BLUR_WORK_RADIUS */
uint32_t yozora_blur_shrink(uint32_t radius);

/* the radius to use on a picture already shrunk by that divisor: at least 1 for a blur that was asked for, so a shrunk picture is never left sharp */
uint32_t yozora_blur_work_radius(uint32_t radius, uint32_t shrink);

/* the bytes of scratch a picture of this size needs, one line of the longer side with four channels, 0 for a size that is not valid */
size_t yozora_blur_scratch_size(uint32_t width, uint32_t height);

/* the mean lightness, 0 to 255, of 4 channel 8 bit pixels in the order red, green, blue, alpha (alpha is not read): the luma of each pixel with the weights
   77, 150 and 29 over 256, summed over the picture and rounded. 0 for an empty size or a stride shorter than a row. the start page uses it to take dark
   or light lettering over a photo */
uint32_t yozora_luma_average(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride);

/* blurs 4 channel 8 bit pixels in place with three passes of a box in each direction, the edge pixels repeat beyond the picture. stride is the
   bytes of a row, scratch_capacity the bytes of scratch. a radius of 0 leaves the pixels as they are. YOZORA_BLUR_INVALID for an empty size, a short
   stride or a radius over 4096, YOZORA_BLUR_SCRATCH when the scratch is smaller than yozora_blur_scratch_size */
yozora_blur_status yozora_blur_rgba(uint8_t *pixels, uint32_t width, uint32_t height, size_t stride, uint32_t radius, uint8_t *scratch,
                                    size_t scratch_capacity);

#ifdef __cplusplus
}
#endif

#endif
