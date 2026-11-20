#ifndef YOZORA_MACHO_H
#define YOZORA_MACHO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_MACHO_COMMAND_LIMIT (1024u * 1024u)

typedef enum {
    YOZORA_MACHO_OK = 0,
    YOZORA_MACHO_INVALID,
    YOZORA_MACHO_MALFORMED,
    YOZORA_MACHO_NO_UUID
} yozora_macho_status;

yozora_macho_status yozora_macho_image_size(const void *header, size_t length, size_t *result);
yozora_macho_status yozora_macho_uuid(const void *image, size_t length, uint8_t *uuid, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
