#include "yozora_macho.h"

#include <string.h>

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

yozora_macho_status yozora_macho_image_size(const void *header, size_t length, size_t *result)
{
    const uint8_t *bytes = (const uint8_t *)header;
    uint32_t magic, commands, size;
    size_t header_size;
    if (!result)
        return YOZORA_MACHO_INVALID;
    *result = 0;
    if (!header)
        return YOZORA_MACHO_INVALID;
    if (length < 28)
        return YOZORA_MACHO_MALFORMED;
    magic = read_u32(bytes);
    /* both supported ios slices use little-endian, thin Mach-O images */
    if (magic == UINT32_C(0xfeedface))
        header_size = 28;
    else if (magic == UINT32_C(0xfeedfacf))
        header_size = 32;
    else
        return YOZORA_MACHO_MALFORMED;
    if (length < header_size)
        return YOZORA_MACHO_MALFORMED;
    commands = read_u32(bytes + 16);
    size = read_u32(bytes + 20);
    if (commands > 4096 || size > YOZORA_MACHO_COMMAND_LIMIT || size < (uint64_t)commands * 8)
        return YOZORA_MACHO_MALFORMED;
    *result = header_size + size;
    return YOZORA_MACHO_OK;
}

yozora_macho_status yozora_macho_uuid(const void *image, size_t length, uint8_t *uuid, size_t capacity)
{
    const uint8_t *bytes = (const uint8_t *)image;
    const uint8_t *found = NULL;
    size_t total, offset;
    uint32_t commands, index;
    yozora_macho_status status;
    if (!uuid || capacity < 16)
        return YOZORA_MACHO_INVALID;
    memset(uuid, 0, 16);
    status = yozora_macho_image_size(image, length, &total);
    if (status != YOZORA_MACHO_OK)
        return status;
    if (total > length)
        return YOZORA_MACHO_MALFORMED;
    offset = read_u32(bytes) == UINT32_C(0xfeedface) ? 28 : 32;
    commands = read_u32(bytes + 16);
    for (index = 0; index < commands; ++index) {
        uint32_t command, size;
        if (total - offset < 8)
            return YOZORA_MACHO_MALFORMED;
        command = read_u32(bytes + offset);
        size = read_u32(bytes + offset + 4);
        if (size < 8 || (size & 3) || size > total - offset)
            return YOZORA_MACHO_MALFORMED;
        if (command == UINT32_C(0x1b)) {
            if (size != 24 || found)
                return YOZORA_MACHO_MALFORMED;
            found = bytes + offset + 8;
        }
        offset += size;
    }
    if (offset != total)
        return YOZORA_MACHO_MALFORMED;
    if (!found)
        return YOZORA_MACHO_NO_UUID;
    memcpy(uuid, found, 16);
    return YOZORA_MACHO_OK;
}
