#include "yozora_memory.h"
#include <assert.h>
#include <mach/mach.h>
#include <stdio.h>

static void release_port(uint32_t port)
{
    assert(mach_port_deallocate(mach_task_self(), port) == KERN_SUCCESS);
}

static unsigned port_count(void)
{
    mach_port_name_array_t names;
    mach_port_type_array_t types;
    mach_msg_type_number_t names_count, types_count;
    assert(mach_port_names(mach_task_self(), &names, &names_count, &types, &types_count) == KERN_SUCCESS);
    assert(vm_deallocate(mach_task_self(), (vm_address_t)(uintptr_t)names, names_count * sizeof(*names)) == KERN_SUCCESS);
    assert(vm_deallocate(mach_task_self(), (vm_address_t)(uintptr_t)types, types_count * sizeof(*types)) == KERN_SUCCESS);
    return names_count;
}

static void shared_memory(void)
{
    void *data = NULL, *readonly = NULL, *private_data = NULL;
    uint32_t port = 0, read_port = 0, copied_port = 0;
    int32_t error = 0;
    size_t size = vm_page_size;
    assert(yozora_memory_allocate(size, &data, &port, &error) == YOZORA_MEMORY_OK);
    assert(data && port && !error);
    assert(!((unsigned char *)data)[0] && !((unsigned char *)data)[size - 1]);
    ((unsigned char *)data)[17] = 42;
    assert(yozora_memory_export(port, size, 0, &read_port, &error) == YOZORA_MEMORY_OK);
    assert(yozora_memory_map(read_port, size, 0, 0, &readonly, &error) == YOZORA_MEMORY_OK);
    assert(((unsigned char *)readonly)[17] == 42);
    ((unsigned char *)data)[17] = 99;
    assert(((unsigned char *)readonly)[17] == 99);
    assert(vm_protect(mach_task_self(), (vm_address_t)(uintptr_t)readonly, size, FALSE, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS);
    assert(yozora_memory_export(read_port, size, 1, &copied_port, &error) == YOZORA_MEMORY_ACCESS && !copied_port);
    assert(yozora_memory_map(port, size, 1, 1, &private_data, &error) == YOZORA_MEMORY_OK);
    ((unsigned char *)private_data)[17] = 11;
    assert(((unsigned char *)data)[17] == 99);
    assert(yozora_memory_share(private_data, size, 0, &copied_port, &error) == YOZORA_MEMORY_OK);
    assert(yozora_memory_deallocate(private_data, size, &error) == YOZORA_MEMORY_OK);
    private_data = NULL;
    assert(yozora_memory_map(copied_port, size, 0, 0, &private_data, &error) == YOZORA_MEMORY_OK);
    assert(((unsigned char *)private_data)[17] == 11);
    ((unsigned char *)data)[17] = 77;
    assert(((unsigned char *)private_data)[17] == 11);
    release_port(copied_port);
    release_port(read_port);
    release_port(port);
    assert(((unsigned char *)readonly)[17] == 77);
    assert(yozora_memory_deallocate(private_data, size, &error) == YOZORA_MEMORY_OK);
    assert(yozora_memory_deallocate(readonly, size, &error) == YOZORA_MEMORY_OK);
    assert(yozora_memory_deallocate(data, size, &error) == YOZORA_MEMORY_OK);
}

static void rejected_input(void)
{
    void *data = (void *)1, *view = (void *)1;
    uint32_t port = UINT32_MAX, other = UINT32_MAX;
    int32_t error = -1;
    size_t size = vm_page_size;
    mach_port_t receive = MACH_PORT_NULL;
    assert(yozora_memory_allocate(0, &data, &port, &error) == YOZORA_MEMORY_INVALID && !data && !port && !error);
    assert(yozora_memory_allocate(UINT64_MAX, &data, &port, &error) == YOZORA_MEMORY_LIMIT && !data && !port);
    assert(yozora_memory_allocate(size, &data, &port, &error) == YOZORA_MEMORY_OK);
    assert(yozora_memory_map(port, size * 2, 0, 0, &view, &error) != YOZORA_MEMORY_OK && !view);
    assert(yozora_memory_map(port, UINT64_MAX, 0, 0, &view, &error) == YOZORA_MEMORY_LIMIT && !view);
    assert(yozora_memory_map(port, size, 4, 0, &view, &error) == YOZORA_MEMORY_INVALID && !view);
    assert(yozora_memory_map(port, size, 0, 4, &view, &error) == YOZORA_MEMORY_INVALID && !view);
    assert(yozora_memory_share((unsigned char *)data + 1, size, 0, &other, &error) == YOZORA_MEMORY_INVALID && !other);
    assert(yozora_memory_share(data, size - 1, 0, &other, &error) == YOZORA_MEMORY_INVALID && !other);
    assert(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &receive) == KERN_SUCCESS);
    assert(yozora_memory_map(receive, size, 0, 0, &view, &error) != YOZORA_MEMORY_OK && !view);
    assert(mach_port_destroy(mach_task_self(), receive) == KERN_SUCCESS);
    release_port(port);
    assert(yozora_memory_deallocate(data, size, &error) == YOZORA_MEMORY_OK);
    assert(yozora_memory_map(port, size, 0, 0, &view, &error) != YOZORA_MEMORY_OK && !view);
}

int main(void)
{
    unsigned i;
    unsigned before;
    shared_memory();
    before = port_count();
    for (i = 0; i < 64; ++i)
        shared_memory();
    rejected_input();
    assert(port_count() == before);
    puts("darwin memory: shared views, readonly rights, private snapshots, lifetime and invalid input passed");
    return 0;
}
