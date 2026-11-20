#include "yozora_darwin.h"
#include "yozora_macho.h"

#include <dlfcn.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>

static pthread_once_t clock_once = PTHREAD_ONCE_INIT;
static mach_timebase_info_data_t timebase;
static kern_return_t timebase_error;
static uint64_t (*continuous_time)(void);
static const vm_size_t *kernel_page_size;
static pthread_mutex_t continuous_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t continuous_last;

static void initialize_clocks(void)
{
    void *symbol = dlsym(RTLD_DEFAULT, "mach_continuous_time");
    /* ios 5 has no continuous mach clock, keep the new symbol optional */
    typedef char same_pointer_size[sizeof(symbol) == sizeof(continuous_time) ? 1 : -1];
    (void)sizeof(same_pointer_size);
    memcpy(&continuous_time, &symbol, sizeof(symbol));
    kernel_page_size = (const vm_size_t *)dlsym(RTLD_DEFAULT, "vm_kernel_page_size");
    timebase_error = mach_timebase_info(&timebase);
}

static yozora_clock_status system_failure(const char *operation, int code)
{
    fprintf(stderr, "yozora: %s failed (%d)\n", operation, code);
    errno = EIO;
    return YOZORA_CLOCK_SYSTEM;
}

static yozora_clock_status ensure_clocks(void)
{
    int error = pthread_once(&clock_once, initialize_clocks);
    if (error)
        return system_failure("pthread_once(clock)", error);
    if (timebase_error != KERN_SUCCESS || !timebase.numer || !timebase.denom)
        return system_failure("mach_timebase_info", timebase_error);
    return YOZORA_CLOCK_OK;
}

yozora_clock_status yozora_darwin_wall_us(uint64_t *result)
{
    struct timeval wall;
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    if (gettimeofday(&wall, NULL))
        return system_failure("gettimeofday", errno);
    return yozora_clock_timeval_us(wall.tv_sec, wall.tv_usec, result);
}

yozora_clock_status yozora_darwin_uptime_ns(uint64_t *result)
{
    yozora_clock_status status;
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    status = ensure_clocks();
    if (status != YOZORA_CLOCK_OK)
        return status;
    return yozora_clock_ticks_to_ns(mach_absolute_time(), timebase.numer, timebase.denom, result);
}

static yozora_clock_status boot_time_us(uint64_t *result)
{
    int name[] = { CTL_KERN, KERN_BOOTTIME };
    struct timeval boot;
    size_t length = sizeof(boot);
    if (sysctl(name, 2, &boot, &length, NULL, 0))
        return system_failure("sysctl(KERN_BOOTTIME)", errno);
    if (length != sizeof(boot))
        return system_failure("sysctl(KERN_BOOTTIME) length", EINVAL);
    return yozora_clock_timeval_us(boot.tv_sec, boot.tv_usec, result);
}

yozora_clock_status yozora_darwin_continuous_ns(uint64_t *result)
{
    yozora_clock_status status;
    uint64_t elapsed = 0;
    unsigned attempt;
    int error;
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    status = ensure_clocks();
    if (status != YOZORA_CLOCK_OK)
        return status;
    if (continuous_time)
        return yozora_clock_ticks_to_ns(continuous_time(), timebase.numer, timebase.denom, result);
    for (attempt = 0; attempt < 4; ++attempt) {
        uint64_t before, wall, after;
        status = boot_time_us(&before);
        if (status != YOZORA_CLOCK_OK)
            return status;
        status = yozora_darwin_wall_us(&wall);
        if (status != YOZORA_CLOCK_OK)
            return status;
        status = boot_time_us(&after);
        if (status != YOZORA_CLOCK_OK)
            return status;
        status = yozora_clock_elapsed_us(wall, before, after, &elapsed);
        if (status != YOZORA_CLOCK_RETRY)
            break;
    }
    if (status != YOZORA_CLOCK_OK)
        return status;
    if (elapsed > UINT64_MAX / 1000)
        return YOZORA_CLOCK_OVERFLOW;
    error = pthread_mutex_lock(&continuous_mutex);
    if (error)
        return system_failure("pthread_mutex_lock(clock)", error);
    /* old kern.boottime has second precision, clamp fractional calendar corrections */
    if (elapsed > continuous_last)
        continuous_last = elapsed;
    *result = continuous_last * 1000;
    error = pthread_mutex_unlock(&continuous_mutex);
    if (error) {
        *result = 0;
        return system_failure("pthread_mutex_unlock(clock)", error);
    }
    return YOZORA_CLOCK_OK;
}

yozora_clock_status yozora_darwin_thread_cpu_us(uint64_t *result)
{
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    uint64_t user, system;
    kern_return_t error;
    yozora_clock_status status;
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    /* pthread_mach_thread_np returns a borrowed port, unlike mach_thread_self */
    error = thread_info(pthread_mach_thread_np(pthread_self()), THREAD_BASIC_INFO,
                        (thread_info_t)&info, &count);
    if (error != KERN_SUCCESS || count != THREAD_BASIC_INFO_COUNT)
        return system_failure("thread_info(THREAD_BASIC_INFO)", error);
    status = yozora_clock_timeval_us(info.user_time.seconds, info.user_time.microseconds, &user);
    if (status != YOZORA_CLOCK_OK)
        return status;
    status = yozora_clock_timeval_us(info.system_time.seconds, info.system_time.microseconds, &system);
    if (status != YOZORA_CLOCK_OK)
        return status;
    if (user > UINT64_MAX - system)
        return YOZORA_CLOCK_OVERFLOW;
    *result = user + system;
    return YOZORA_CLOCK_OK;
}

yozora_clock_status yozora_darwin_memory_bytes(size_t *result)
{
    task_basic_info_data_t info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    kern_return_t error;
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    /* ios 5 lacks TASK_VM_INFO phys_footprint, resident pages are the available metric */
    error = task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count);
    if (error != KERN_SUCCESS || count != TASK_BASIC_INFO_COUNT)
        return system_failure("task_info(TASK_BASIC_INFO)", error);
    if (info.resident_size > SIZE_MAX)
        return YOZORA_CLOCK_OVERFLOW;
    *result = (size_t)info.resident_size;
    return YOZORA_CLOCK_OK;
}

yozora_clock_status yozora_darwin_kernel_page_size(size_t *result)
{
    yozora_clock_status status;
    vm_size_t value;
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    status = ensure_clocks();
    if (status != YOZORA_CLOCK_OK)
        return status;
    /* ios 5 has one page size, later kernels may use larger physical pages */
    value = kernel_page_size ? *kernel_page_size : vm_page_size;
    if (!value || (value & (value - 1)))
        return YOZORA_CLOCK_INVALID;
    *result = value;
    return YOZORA_CLOCK_OK;
}

int yozora_darwin_image_uuid(const void *address, uint8_t *uuid, size_t capacity)
{
    Dl_info info;
    size_t size = 0;
    if (!uuid || capacity < 16)
        return EINVAL;
    memset(uuid, 0, 16);
    if (!address)
        return EINVAL;
    memset(&info, 0, sizeof(info));
    if (!dladdr(address, &info) || !info.dli_fbase)
        return ENOENT;
    /* dyld owns the mapped image, parse its real UUID without newer private exports */
    if (yozora_macho_image_size(info.dli_fbase, 32, &size) != YOZORA_MACHO_OK)
        return EINVAL;
    switch (yozora_macho_uuid(info.dli_fbase, size, uuid, capacity)) {
    case YOZORA_MACHO_OK:
        return 0;
    case YOZORA_MACHO_NO_UUID:
        return ENOENT;
    default:
        return EINVAL;
    }
}
