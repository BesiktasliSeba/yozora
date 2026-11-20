#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "yozora_types.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <hb-ft.h>
#include <hb-icu.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FONT_LIMIT (32 * 1024 * 1024)
#define TEXT_CAPACITY 4097
#define IMAGE_WIDTH 1024
#define IMAGE_HEIGHT 128

static unsigned char *read_font(const char *path, size_t *length)
{
    struct stat info;
    unsigned char *bytes = NULL;
    FILE *file = NULL;
    int descriptor = open(path, O_RDONLY | O_NONBLOCK);
    *length = 0;
    if (descriptor < 0) {
        fprintf(stderr, "yozora-text: open font: %s\n", strerror(errno));
        return NULL;
    }
    if (fstat(descriptor, &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || info.st_size > FONT_LIMIT) {
        fputs("yozora-text: font must be a regular file of 1 byte to 32 MiB\n", stderr);
        goto close_descriptor;
    }
    file = fdopen(descriptor, "rb");
    if (!file) {
        fprintf(stderr, "yozora-text: fdopen: %s\n", strerror(errno));
        goto close_descriptor;
    }
    bytes = malloc((size_t)info.st_size);
    if (!bytes) {
        fputs("yozora-text: cannot allocate font buffer\n", stderr);
    } else if (fread(bytes, 1, (size_t)info.st_size, file) != (size_t)info.st_size ||
               fgetc(file) != EOF || ferror(file)) {
        fputs("yozora-text: font read failed or size changed\n", stderr);
        free(bytes);
        bytes = NULL;
    }
    if (fclose(file)) {
        fputs("yozora-text: font close failed\n", stderr);
        free(bytes);
        return NULL;
    }
    if (bytes)
        *length = (size_t)info.st_size;
    return bytes;

close_descriptor:
    if (close(descriptor))
        fputs("yozora-text: font descriptor close failed\n", stderr);
    return NULL;
}

static int paint(FT_Face face, hb_glyph_info_t *glyphs, hb_glyph_position_t *positions,
                 unsigned count, unsigned char *pixels, unsigned *ink)
{
    int64_t pen_x = 4 * 64, pen_y = 0;
    unsigned index;
    *ink = 0;
    for (index = 0; index < count; ++index) {
        FT_Bitmap *bitmap;
        int64_t x, y;
        size_t stride;
        unsigned row, column;
        FT_BBox bounds;
        if (!glyphs[index].codepoint) {
            fputs("yozora-text: font has no glyph for part of the text\n", stderr);
            return 0;
        }
        if (FT_Load_Glyph(face, glyphs[index].codepoint, FT_LOAD_NO_BITMAP) ||
            face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
            fputs("yozora-text: cannot load an outline glyph\n", stderr);
            return 0;
        }
        /* reject large outlines before freetype allocates a raster buffer */
        FT_Outline_Get_CBox(&face->glyph->outline, &bounds);
        if (bounds.xMin < -IMAGE_WIDTH * 64 || bounds.xMax > IMAGE_WIDTH * 64 ||
            bounds.yMin < -IMAGE_HEIGHT * 64 || bounds.yMax > IMAGE_HEIGHT * 64 ||
            bounds.xMax < bounds.xMin || bounds.yMax < bounds.yMin ||
            bounds.xMax - bounds.xMin > IMAGE_WIDTH * 64 ||
            bounds.yMax - bounds.yMin > IMAGE_HEIGHT * 64) {
            fputs("yozora-text: glyph outline exceeds the canvas\n", stderr);
            return 0;
        }
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
            fputs("yozora-text: glyph rasterization failed\n", stderr);
            return 0;
        }
        bitmap = &face->glyph->bitmap;
        x = (pen_x + positions[index].x_offset) / 64 + face->glyph->bitmap_left;
        y = 64 - (pen_y + positions[index].y_offset) / 64 - face->glyph->bitmap_top;
        if (bitmap->width && bitmap->rows) {
            stride = (size_t)(bitmap->pitch < 0 ? -(int64_t)bitmap->pitch : bitmap->pitch);
            if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays != 256 ||
                !bitmap->buffer || stride < bitmap->width || stride > IMAGE_WIDTH ||
                x < 0 || y < 0 || x + bitmap->width > IMAGE_WIDTH ||
                y + bitmap->rows > IMAGE_HEIGHT) {
                fputs("yozora-text: bitmap is unsupported or exceeds the 1024x128 canvas\n", stderr);
                return 0;
            }
            for (row = 0; row < bitmap->rows; ++row) {
                size_t source_row = bitmap->pitch < 0 ? bitmap->rows - 1 - row : row;
                const unsigned char *source = bitmap->buffer + source_row * stride;
                unsigned char *target = pixels + ((size_t)y + row) * IMAGE_WIDTH + (size_t)x;
                for (column = 0; column < bitmap->width; ++column) {
                    if (source[column])
                        ++*ink;
                    target[column] = (unsigned char)((unsigned)target[column] * (255 - source[column]) / 255);
                }
            }
        }
        pen_x += positions[index].x_advance;
        pen_y += positions[index].y_advance;
        if (pen_x < 0 || pen_x > IMAGE_WIDTH * 64 ||
            pen_y < -IMAGE_HEIGHT * 64 || pen_y > IMAGE_HEIGHT * 64) {
            fputs("yozora-text: glyph advances exceed the canvas\n", stderr);
            return 0;
        }
    }
    return 1;
}

static int write_image(const char *path, const unsigned char *pixels)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    FILE *file;
    int success;
    if (descriptor < 0) {
        fprintf(stderr, "yozora-text: create output: %s\n", strerror(errno));
        return 0;
    }
    file = fdopen(descriptor, "wb");
    if (!file) {
        fprintf(stderr, "yozora-text: output fdopen: %s\n", strerror(errno));
        if (close(descriptor))
            fputs("yozora-text: output descriptor close failed\n", stderr);
        success = 0;
    } else {
        success = fprintf(file, "P5\n%d %d\n255\n", IMAGE_WIDTH, IMAGE_HEIGHT) > 0 &&
                  fwrite(pixels, 1, IMAGE_WIDTH * IMAGE_HEIGHT, file) == IMAGE_WIDTH * IMAGE_HEIGHT;
        if (fclose(file))
            success = 0;
    }
    if (!success) {
        fputs("yozora-text: output write failed\n", stderr);
        if (unlink(path))
            fprintf(stderr, "yozora-text: output cleanup: %s\n", strerror(errno));
    }
    return success;
}

int main(int argc, char **argv)
{
    unsigned char *bytes = NULL, *pixels = NULL;
    size_t length = 0, index;
    FT_Library library = NULL;
    FT_Face face = NULL;
    hb_font_t *font = NULL;
    hb_buffer_t *buffer = NULL;
    hb_unicode_funcs_t *unicode;
    hb_codepoint_t composed = 0;
    hb_glyph_info_t *glyphs;
    hb_glyph_position_t *positions;
    unsigned count = 0, position_count = 0, ink = 0;
    int status = 1, major, minor, patch;
    if (argc != 4) {
        fputs("usage: yozora-text FONT TEXT OUTPUT.pgm\n", stderr);
        return 2;
    }
    if (yozora_text_check(argv[2], TEXT_CAPACITY, 0) != YOZORA_OK) {
        fputs("yozora-text: text must be valid UTF-8 of 1 to 4096 bytes\n", stderr);
        return 2;
    }
    for (index = 0; argv[2][index]; ++index) {
        if ((unsigned char)argv[2][index] < 32 || argv[2][index] == 127) {
            fputs("yozora-text: text must be one line without control bytes\n", stderr);
            return 2;
        }
    }
    unicode = hb_icu_get_unicode_funcs();
    if (!hb_unicode_compose(unicode, 'e', 0x301, &composed) || composed != 0xe9 ||
        hb_unicode_combining_class(unicode, 0x301) != 230) {
        fputs("yozora-text: ICU unicode functions failed\n", stderr);
        return 1;
    }
    bytes = read_font(argv[1], &length);
    if (!bytes)
        return 2;
    if (FT_Init_FreeType(&library) || FT_New_Memory_Face(library, bytes, (FT_Long)length, 0, &face) ||
        FT_Select_Charmap(face, FT_ENCODING_UNICODE) || FT_Set_Pixel_Sizes(face, 0, 32)) {
        fputs("yozora-text: cannot initialize a Unicode font\n", stderr);
        goto cleanup;
    }
    font = hb_ft_font_create_referenced(face);
    hb_ft_font_set_load_flags(font, FT_LOAD_NO_BITMAP);
    buffer = hb_buffer_create();
    hb_buffer_set_unicode_funcs(buffer, unicode);
    hb_buffer_add_utf8(buffer, argv[2], -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, NULL, 0);
    if (!hb_buffer_allocation_successful(buffer)) {
        fputs("yozora-text: shaping allocation failed\n", stderr);
        goto cleanup;
    }
    glyphs = hb_buffer_get_glyph_infos(buffer, &count);
    positions = hb_buffer_get_glyph_positions(buffer, &position_count);
    if (!count || count > TEXT_CAPACITY - 1 || count != position_count) {
        fputs("yozora-text: invalid or oversized glyph run\n", stderr);
        goto cleanup;
    }
    pixels = malloc(IMAGE_WIDTH * IMAGE_HEIGHT);
    if (!pixels) {
        fputs("yozora-text: cannot allocate canvas\n", stderr);
        goto cleanup;
    }
    memset(pixels, 255, IMAGE_WIDTH * IMAGE_HEIGHT);
    if (!paint(face, glyphs, positions, count, pixels, &ink) || !write_image(argv[3], pixels))
        goto cleanup;
    FT_Library_Version(library, &major, &minor, &patch);
    printf("{\"harfbuzz\":\"%s\",\"freetype\":\"%d.%d.%d\",\"glyphs\":%u,\"ink\":%u,\"run\":[",
           hb_version_string(), major, minor, patch, count, ink);
    for (index = 0; index < count; ++index)
        printf("%s[%u,%u,%d,%d,%d,%d]", index ? "," : "", glyphs[index].codepoint,
               glyphs[index].cluster, positions[index].x_advance, positions[index].y_advance,
               positions[index].x_offset, positions[index].y_offset);
    fputs("]}\n", stdout);
    status = fflush(stdout) ? 1 : 0;
    if (status)
        fputs("yozora-text: result write failed\n", stderr);

cleanup:
    if (buffer)
        hb_buffer_destroy(buffer);
    if (font)
        hb_font_destroy(font);
    if (face && FT_Done_Face(face)) {
        fputs("yozora-text: font teardown failed\n", stderr);
        status = 1;
    }
    if (library && FT_Done_FreeType(library)) {
        fputs("yozora-text: library teardown failed\n", stderr);
        status = 1;
    }
    free(pixels);
    free(bytes);
    return status;
}
