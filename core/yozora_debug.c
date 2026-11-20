#include "yozora_debug.h"

#include <stdio.h>
#include <string.h>

static unsigned long long traffic_received;
static unsigned long long traffic_sent;

void yozora_traffic_add(unsigned long long received, unsigned long long sent)
{
    if (received)
        __atomic_fetch_add(&traffic_received, received, __ATOMIC_RELAXED);
    if (sent)
        __atomic_fetch_add(&traffic_sent, sent, __ATOMIC_RELAXED);
}

void yozora_traffic_read(unsigned long long *received, unsigned long long *sent)
{
    if (received)
        *received = __atomic_load_n(&traffic_received, __ATOMIC_RELAXED);
    if (sent)
        *sent = __atomic_load_n(&traffic_sent, __ATOMIC_RELAXED);
}

typedef struct {
    const char *identifier;
    const char *name;
} model_name;

/* the phones and tablets that run ios 5 and later, newest last in each family */
static const model_name models[] = {
    { "iPhone3,1", "iPhone 4" }, { "iPhone3,2", "iPhone 4" }, { "iPhone3,3", "iPhone 4" },
    { "iPhone4,1", "iPhone 4S" },
    { "iPhone5,1", "iPhone 5" }, { "iPhone5,2", "iPhone 5" },
    { "iPhone5,3", "iPhone 5c" }, { "iPhone5,4", "iPhone 5c" },
    { "iPhone6,1", "iPhone 5s" }, { "iPhone6,2", "iPhone 5s" },
    { "iPhone7,2", "iPhone 6" }, { "iPhone7,1", "iPhone 6 Plus" },
    { "iPhone8,1", "iPhone 6s" }, { "iPhone8,2", "iPhone 6s Plus" }, { "iPhone8,4", "iPhone SE" },
    { "iPhone9,1", "iPhone 7" }, { "iPhone9,3", "iPhone 7" }, { "iPhone9,2", "iPhone 7 Plus" }, { "iPhone9,4", "iPhone 7 Plus" },
    { "iPhone10,1", "iPhone 8" }, { "iPhone10,4", "iPhone 8" }, { "iPhone10,2", "iPhone 8 Plus" }, { "iPhone10,5", "iPhone 8 Plus" },
    { "iPhone10,3", "iPhone X" }, { "iPhone10,6", "iPhone X" },
    { "iPhone11,8", "iPhone XR" }, { "iPhone11,2", "iPhone XS" }, { "iPhone11,4", "iPhone XS Max" }, { "iPhone11,6", "iPhone XS Max" },
    { "iPhone12,1", "iPhone 11" }, { "iPhone12,3", "iPhone 11 Pro" }, { "iPhone12,5", "iPhone 11 Pro Max" }, { "iPhone12,8", "iPhone SE 2" },
    { "iPhone13,1", "iPhone 12 mini" }, { "iPhone13,2", "iPhone 12" }, { "iPhone13,3", "iPhone 12 Pro" }, { "iPhone13,4", "iPhone 12 Pro Max" },
    { "iPhone14,4", "iPhone 13 mini" }, { "iPhone14,5", "iPhone 13" }, { "iPhone14,2", "iPhone 13 Pro" }, { "iPhone14,3", "iPhone 13 Pro Max" },
    { "iPhone14,6", "iPhone SE 3" },
    { "iPhone14,7", "iPhone 14" }, { "iPhone14,8", "iPhone 14 Plus" }, { "iPhone15,2", "iPhone 14 Pro" }, { "iPhone15,3", "iPhone 14 Pro Max" },
    { "iPhone15,4", "iPhone 15" }, { "iPhone15,5", "iPhone 15 Plus" }, { "iPhone16,1", "iPhone 15 Pro" }, { "iPhone16,2", "iPhone 15 Pro Max" },
    { "iPhone17,3", "iPhone 16" }, { "iPhone17,4", "iPhone 16 Plus" }, { "iPhone17,1", "iPhone 16 Pro" }, { "iPhone17,2", "iPhone 16 Pro Max" },
    { "iPad2,1", "iPad 2" }, { "iPad2,2", "iPad 2" }, { "iPad2,3", "iPad 2" }, { "iPad2,4", "iPad 2" },
    { "iPad3,1", "iPad 3" }, { "iPad3,2", "iPad 3" }, { "iPad3,3", "iPad 3" },
    { "iPad3,4", "iPad 4" }, { "iPad3,5", "iPad 4" }, { "iPad3,6", "iPad 4" },
    { "iPad2,5", "iPad mini" }, { "iPad2,6", "iPad mini" }, { "iPad2,7", "iPad mini" },
    { "iPad4,1", "iPad Air" }, { "iPad4,2", "iPad Air" }, { "iPad4,3", "iPad Air" },
    { "iPad4,4", "iPad mini 2" }, { "iPad4,5", "iPad mini 2" }, { "iPad4,6", "iPad mini 2" },
    { "iPad4,7", "iPad mini 3" }, { "iPad4,8", "iPad mini 3" }, { "iPad4,9", "iPad mini 3" },
    { "iPad5,1", "iPad mini 4" }, { "iPad5,2", "iPad mini 4" }, { "iPad5,3", "iPad Air 2" }, { "iPad5,4", "iPad Air 2" },
    { "iPod5,1", "iPod touch 5" }, { "iPod7,1", "iPod touch 6" }, { "iPod9,1", "iPod touch 7" },
};

void yozora_device_model_name(const char *identifier, char *out, size_t capacity)
{
    if (!out || !capacity)
        return;
    out[0] = 0;
    if (!identifier)
        return;
    for (size_t i = 0; i < sizeof models / sizeof models[0]; ++i) {
        if (!strcmp(models[i].identifier, identifier)) {
            snprintf(out, capacity, "%s", models[i].name);
            return;
        }
    }
    snprintf(out, capacity, "%s", identifier);
}

void yozora_format_bytes(unsigned long long bytes, char *out, size_t capacity)
{
    static const char *const units[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
    if (!out || !capacity)
        return;
    if (bytes < 1024) {
        snprintf(out, capacity, "%llu B", bytes);
        return;
    }
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024 && unit < 6) {
        value /= 1024;
        ++unit;
    }
    snprintf(out, capacity, unit >= 3 ? "%.2f %s" : "%.1f %s", value, units[unit]);
}
