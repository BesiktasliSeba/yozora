#include "yozora_macho.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void check_image(uint32_t magic, size_t header_size)
{
    uint8_t image[96] = { 0 }, uuid[16];
    size_t i, total = header_size + 24, result = 1;
    write_u32(image, magic);
    write_u32(image + 16, 1);
    write_u32(image + 20, 24);
    write_u32(image + header_size, 0x1b);
    write_u32(image + header_size + 4, 24);
    for (i = 0; i < 16; ++i)
        image[header_size + 8 + i] = (uint8_t)(i + 1);
    assert(yozora_macho_image_size(image, header_size, &result) == YOZORA_MACHO_OK && result == total);
    assert(yozora_macho_uuid(image, total, uuid, sizeof(uuid)) == YOZORA_MACHO_OK);
    assert(!memcmp(uuid, image + header_size + 8, 16));
    for (i = 0; i < total; ++i)
        assert(yozora_macho_uuid(image, i, uuid, sizeof(uuid)) == YOZORA_MACHO_MALFORMED);
    assert(yozora_macho_uuid(image, total, uuid, 15) == YOZORA_MACHO_INVALID);
    write_u32(image + header_size + 4, 0);
    assert(yozora_macho_uuid(image, total, uuid, sizeof(uuid)) == YOZORA_MACHO_MALFORMED);
    write_u32(image + header_size + 4, 25);
    assert(yozora_macho_uuid(image, total + 1, uuid, sizeof(uuid)) == YOZORA_MACHO_MALFORMED);
    write_u32(image + header_size + 4, 24);
    write_u32(image + 16, 2);
    write_u32(image + 20, 48);
    memcpy(image + total, image + header_size, 24);
    assert(yozora_macho_uuid(image, total + 24, uuid, sizeof(uuid)) == YOZORA_MACHO_MALFORMED);
    write_u32(image + total, 1);
    assert(yozora_macho_uuid(image, total + 24, uuid, sizeof(uuid)) == YOZORA_MACHO_OK);
    write_u32(image + header_size, 1);
    assert(yozora_macho_uuid(image, total + 24, uuid, sizeof(uuid)) == YOZORA_MACHO_NO_UUID);
    write_u32(image + 16, 0);
    write_u32(image + 20, 0);
    assert(yozora_macho_uuid(image, header_size, uuid, sizeof(uuid)) == YOZORA_MACHO_NO_UUID);
    write_u32(image + 20, YOZORA_MACHO_COMMAND_LIMIT + 1);
    assert(yozora_macho_image_size(image, sizeof(image), &result) == YOZORA_MACHO_MALFORMED && result == 0);
    write_u32(image + 20, YOZORA_MACHO_COMMAND_LIMIT);
    write_u32(image + 16, 4096);
    assert(yozora_macho_image_size(image, sizeof(image), &result) == YOZORA_MACHO_OK);
    write_u32(image + 16, 4097);
    assert(yozora_macho_image_size(image, sizeof(image), &result) == YOZORA_MACHO_MALFORMED && result == 0);
}

int main(void)
{
    uint8_t invalid[32] = { 0 }, uuid[16];
    size_t result = 1;
    assert(yozora_macho_image_size(NULL, 32, &result) == YOZORA_MACHO_INVALID && result == 0);
    assert(yozora_macho_image_size(invalid, sizeof(invalid), NULL) == YOZORA_MACHO_INVALID);
    assert(yozora_macho_uuid(invalid, sizeof(invalid), NULL, 16) == YOZORA_MACHO_INVALID);
    assert(yozora_macho_uuid(invalid, sizeof(invalid), uuid, 16) == YOZORA_MACHO_MALFORMED);
    check_image(UINT32_C(0xfeedface), 28);
    check_image(UINT32_C(0xfeedfacf), 32);
    puts("Mach-O: both headers, real UUID bytes, truncation, commands, duplicate UUID and size bounds passed");
    return 0;
}
