#include "yozora_memory.h"

yozora_memory_status yozora_memory_round_size(uint64_t size, uint64_t page_size, uint64_t *rounded)
{
    if (!rounded)
        return YOZORA_MEMORY_INVALID;
    *rounded = 0;
    if (!size || !page_size || page_size > 65536 || (page_size & (page_size - 1)))
        return YOZORA_MEMORY_INVALID;
    if (size > YOZORA_MEMORY_MAX_SIZE)
        return YOZORA_MEMORY_LIMIT;
    *rounded = (size + page_size - 1) & ~(page_size - 1);
    return YOZORA_MEMORY_OK;
}

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/memory_object_types.h>
#include <stdio.h>

static yozora_memory_status mach_result(kern_return_t result, int32_t *kernel_error)
{
    *kernel_error = result;
    if (result == KERN_SUCCESS)
        return YOZORA_MEMORY_OK;
    return result == KERN_PROTECTION_FAILURE ? YOZORA_MEMORY_ACCESS : YOZORA_MEMORY_SYSTEM;
}

static yozora_memory_status memory_size(uint64_t size, uint64_t *rounded, int32_t *kernel_error)
{
    if (!kernel_error)
        return YOZORA_MEMORY_INVALID;
    *kernel_error = 0;
    return yozora_memory_round_size(size, vm_page_size, rounded);
}

yozora_memory_status yozora_memory_export(uint32_t parent, uint64_t size, int writable, uint32_t *port, int32_t *kernel_error)
{
    uint64_t rounded;
    memory_object_size_t entry_size;
    mach_port_t result = MACH_PORT_NULL;
    kern_return_t error;
    yozora_memory_status status;
    if (port)
        *port = 0;
    status = memory_size(size, &rounded, kernel_error);
    if (status != YOZORA_MEMORY_OK)
        return status;
    if (!port || !MACH_PORT_VALID(parent) || (writable != 0 && writable != 1))
        return YOZORA_MEMORY_INVALID;
    entry_size = rounded;
    /* a null task rejects non-memory ports instead of treating them as local address ranges */
    error = mach_make_memory_entry_64(MACH_PORT_NULL, &entry_size, 0,
        VM_PROT_READ | (writable ? VM_PROT_WRITE : 0), &result, parent);
    if (error != KERN_SUCCESS)
        return mach_result(error, kernel_error);
    if (entry_size != rounded || !MACH_PORT_VALID(result)) {
        if (MACH_PORT_VALID(result)) {
            error = mach_port_deallocate(mach_task_self(), result);
            if (error != KERN_SUCCESS)
                return mach_result(error, kernel_error);
        }
        return YOZORA_MEMORY_INVALID;
    }
    *port = result;
    return YOZORA_MEMORY_OK;
}

yozora_memory_status yozora_memory_map(uint32_t port, uint64_t size, int writable, int copy_on_write, void **data, int32_t *kernel_error)
{
    uint64_t rounded;
    uint32_t restricted_port = 0;
    vm_address_t address = 0;
    vm_prot_t protection = VM_PROT_READ | (writable ? VM_PROT_WRITE : 0);
    kern_return_t mapped, released;
    yozora_memory_status status;
    if (data)
        *data = NULL;
    status = memory_size(size, &rounded, kernel_error);
    if (status != YOZORA_MEMORY_OK)
        return status;
    if (!data || (copy_on_write != 0 && copy_on_write != 1))
        return YOZORA_MEMORY_INVALID;
    status = yozora_memory_export(port, size, writable, &restricted_port, kernel_error);
    if (status != YOZORA_MEMORY_OK)
        return status;
    /* the maximum protection is also restricted, mprotect cannot upgrade a read-only import */
    mapped = vm_map(mach_task_self(), &address, (vm_size_t)rounded, 0, VM_FLAGS_ANYWHERE,
        restricted_port, 0, copy_on_write, protection, protection, VM_INHERIT_NONE);
    released = mach_port_deallocate(mach_task_self(), restricted_port);
    if (released != KERN_SUCCESS) {
        if (mapped == KERN_SUCCESS) {
            kern_return_t cleanup = vm_deallocate(mach_task_self(), address, (vm_size_t)rounded);
            if (cleanup != KERN_SUCCESS)
                fprintf(stderr, "yozora memory: release port failed (%d), release mapping failed (%d)\n", released, cleanup);
        } else
            fprintf(stderr, "yozora memory: map failed (%d), release port failed (%d)\n", mapped, released);
        return mach_result(released, kernel_error);
    }
    if (mapped != KERN_SUCCESS)
        return mach_result(mapped, kernel_error);
    *data = (void *)(uintptr_t)address;
    return YOZORA_MEMORY_OK;
}

yozora_memory_status yozora_memory_allocate(uint64_t size, void **data, uint32_t *port, int32_t *kernel_error)
{
    uint64_t rounded;
    memory_object_size_t entry_size;
    mach_port_t backing = MACH_PORT_NULL;
    kern_return_t result;
    yozora_memory_status status;
    if (data)
        *data = NULL;
    if (port)
        *port = 0;
    status = memory_size(size, &rounded, kernel_error);
    if (status != YOZORA_MEMORY_OK)
        return status;
    if (!data || !port)
        return YOZORA_MEMORY_INVALID;
    entry_size = rounded;
    /* ios 5 named memory objects keep their size fixed without a filesystem backing */
    result = mach_make_memory_entry_64(MACH_PORT_NULL, &entry_size, 0,
        VM_PROT_READ | VM_PROT_WRITE | MAP_MEM_NAMED_CREATE, &backing, MACH_PORT_NULL);
    if (result != KERN_SUCCESS)
        return mach_result(result, kernel_error);
    if (entry_size != rounded || !MACH_PORT_VALID(backing)) {
        if (MACH_PORT_VALID(backing)) {
            result = mach_port_deallocate(mach_task_self(), backing);
            if (result != KERN_SUCCESS)
                return mach_result(result, kernel_error);
        }
        return YOZORA_MEMORY_INVALID;
    }
    status = yozora_memory_map(backing, size, 1, 0, data, kernel_error);
    if (status != YOZORA_MEMORY_OK) {
        result = mach_port_deallocate(mach_task_self(), backing);
        if (result != KERN_SUCCESS)
            return mach_result(result, kernel_error);
        return status;
    }
    *port = backing;
    return YOZORA_MEMORY_OK;
}

yozora_memory_status yozora_memory_share(void *data, uint64_t size, int writable, uint32_t *port, int32_t *kernel_error)
{
    uint64_t rounded;
    memory_object_size_t entry_size;
    mach_port_t result = MACH_PORT_NULL;
    kern_return_t error;
    yozora_memory_status status;
    if (port)
        *port = 0;
    status = memory_size(size, &rounded, kernel_error);
    if (status != YOZORA_MEMORY_OK)
        return status;
    if (!data || !port || (writable != 0 && writable != 1)
        || (uintptr_t)data % vm_page_size || size != rounded || (uintptr_t)data > UINTPTR_MAX - size)
        return YOZORA_MEMORY_INVALID;
    entry_size = rounded;
    error = mach_make_memory_entry_64(mach_task_self(), &entry_size, (memory_object_offset_t)(uintptr_t)data,
        VM_PROT_READ | (writable ? VM_PROT_WRITE : 0), &result, MACH_PORT_NULL);
    if (error != KERN_SUCCESS)
        return mach_result(error, kernel_error);
    if (entry_size != rounded || !MACH_PORT_VALID(result)) {
        if (MACH_PORT_VALID(result)) {
            error = mach_port_deallocate(mach_task_self(), result);
            if (error != KERN_SUCCESS)
                return mach_result(error, kernel_error);
        }
        return YOZORA_MEMORY_INVALID;
    }
    *port = result;
    return YOZORA_MEMORY_OK;
}

yozora_memory_status yozora_memory_deallocate(void *data, uint64_t size, int32_t *kernel_error)
{
    uint64_t rounded;
    yozora_memory_status status = memory_size(size, &rounded, kernel_error);
    if (status != YOZORA_MEMORY_OK)
        return status;
    if (!data || (uintptr_t)data % vm_page_size || (uintptr_t)data > UINTPTR_MAX - size)
        return YOZORA_MEMORY_INVALID;
    return mach_result(vm_deallocate(mach_task_self(), (vm_address_t)(uintptr_t)data, (vm_size_t)rounded), kernel_error);
}
#endif
