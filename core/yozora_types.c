#include "yozora_types.h"

const char *yozora_status_string(yozora_status status) {
    switch (status) {
        case YOZORA_OK: return "ok";
        case YOZORA_INVALID: return "invalid input";
        case YOZORA_LIMIT: return "size or count limit exceeded";
        case YOZORA_NOMEM: return "out of memory";
        case YOZORA_NOT_FOUND: return "entry not found";
        case YOZORA_STALE: return "stale navigation callback";
        case YOZORA_STATE: return "invalid navigation state";
        case YOZORA_BUSY: return "database is busy";
        case YOZORA_IO: return "file access failed";
        case YOZORA_STORAGE: return "database operation failed";
        case YOZORA_CORRUPT: return "invalid database contents";
        case YOZORA_SCHEMA: return "unsupported database schema";
    }
    return "unknown status";
}

yozora_status yozora_text_check(const char *text, size_t capacity, int allow_empty) {
    size_t length = 0, i = 0;
    if (!text || !capacity) return YOZORA_INVALID;
    while (length < capacity && text[length]) ++length;
    if (length == capacity) return YOZORA_LIMIT;
    if (!length && !allow_empty) return YOZORA_INVALID;
    while (i < length) {
        unsigned char first = (unsigned char)text[i++];
        uint32_t value, minimum;
        size_t continuation, j;
        if (first < 0x80) continue;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1; value = first & 0x1f; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2; value = first & 0x0f; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3; value = first & 0x07; minimum = 0x10000;
        } else {
            return YOZORA_INVALID;
        }
        if (continuation > length - i) return YOZORA_INVALID;
        for (j = 0; j < continuation; ++j) {
            unsigned char byte = (unsigned char)text[i++];
            if ((byte & 0xc0) != 0x80) return YOZORA_INVALID;
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return YOZORA_INVALID;
    }
    return YOZORA_OK;
}

yozora_status yozora_url_text_check(const char *url) {
    yozora_status status = yozora_text_check(url, YOZORA_URL_CAP, 0);
    size_t i;
    if (status != YOZORA_OK) return status;
    for (i = 0; url[i]; ++i) {
        unsigned char byte = (unsigned char)url[i];
        if (byte <= 0x20 || byte == 0x7f) return YOZORA_INVALID;
    }
    return YOZORA_OK;
}
