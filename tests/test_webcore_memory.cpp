#include "config.h"
#include "SharedBuffer.h"
#include "SharedMemory.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <unistd.h>
#include <wtf/MainThread.h>

// release WTF omits debug assertion helpers, enable C assertions after its headers
#undef NDEBUG
#include <cassert>

using namespace WebCore;
using Protection = SharedMemory::Protection;

static size_t descriptor_count()
{
    DIR* directory = opendir("/proc/self/fd");
    assert(directory);
    size_t count = 0;
    while (auto* entry = readdir(directory)) {
        if (entry->d_name[0] != '.')
            ++count;
    }
    assert(!closedir(directory));
    return count;
}

static void shared_views()
{
    auto memory = SharedMemory::allocate(4096);
    assert(memory && memory->size() == 4096);
    for (auto byte : memory->span())
        assert(!byte);
    memory->mutableSpan()[17] = 42;
    auto handle = memory->createHandle(Protection::ReadWrite);
    assert(handle);
    SharedMemoryHandle duplicate(*handle);
    auto view = SharedMemory::map(WTF::move(duplicate), Protection::ReadWrite);
    assert(view && view->span()[17] == 42);
    view->mutableSpan()[18] = 99;
    assert(memory->span()[18] == 99);
    memory = nullptr;
    auto exported = view->createHandle(Protection::ReadWrite);
    assert(exported);
    auto another = SharedMemory::map(WTF::move(*exported), Protection::ReadWrite);
    assert(another && another->span()[18] == 99);
    auto descriptor = handle->releaseHandle();
    assert(descriptor && (fcntl(descriptor.value(), F_GETFD) & FD_CLOEXEC));
    assert(ftruncate(descriptor.value(), 0) < 0 && errno == EPERM);
    assert(ftruncate(descriptor.value(), 8192) < 0 && errno == EPERM);
}

static void readonly_views()
{
    auto memory = SharedMemory::allocate(4096);
    assert(memory);
    memory->mutableSpan()[0] = 7;
    auto handle = memory->createHandle(Protection::ReadOnly);
    assert(handle);
    SharedMemoryHandle writable_attempt(*handle);
    assert(!SharedMemory::map(WTF::move(writable_attempt), Protection::ReadWrite));
    auto view = SharedMemory::map(WTF::move(*handle), Protection::ReadOnly);
    assert(view && view->span()[0] == 7);
    assert(!view->createHandle(Protection::ReadWrite));
    assert(mprotect(const_cast<uint8_t*>(view->span().data()), view->size(), PROT_READ | PROT_WRITE) < 0);
    auto exported = view->createHandle(Protection::ReadOnly);
    assert(exported);
    auto descriptor = exported->releaseHandle();
    assert((fcntl(descriptor.value(), F_GETFL) & O_ACCMODE) == O_RDONLY);
    assert(fcntl(descriptor.value(), F_GETFD) & FD_CLOEXEC);
    char byte = 1;
    assert(pwrite(descriptor.value(), &byte, 1, 0) < 0 && errno == EBADF);
    memory->mutableSpan()[0] = 9;
    assert(view->span()[0] == 9);
}

static void private_views()
{
    auto memory = SharedMemory::allocate(4096);
    assert(memory);
    memory->mutableSpan()[0] = 12;
    auto handle = memory->createHandle(Protection::ReadOnly);
    assert(handle);
    auto view = SharedMemory::map(WTF::move(*handle), Protection::ReadWrite, SharedMemory::CopyOnWrite::Yes);
    assert(view && view->span()[0] == 12);
    view->mutableSpan()[0] = 21;
    assert(memory->span()[0] == 12);
    auto snapshot = view->createHandle(Protection::ReadOnly);
    assert(snapshot);
    view->mutableSpan()[0] = 31;
    auto copied = SharedMemory::map(WTF::move(*snapshot), Protection::ReadOnly);
    assert(copied && copied->span()[0] == 21);
}

static void borrowed_views()
{
    auto memory = SharedMemory::allocate(4096);
    assert(memory);
    auto handle = memory->createHandle(Protection::ReadWrite);
    assert(handle);
    auto descriptor = handle->releaseHandle();
    void* data = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor.value(), 0);
    assert(data != MAP_FAILED);
    {
        auto borrowed = SharedMemory::wrapMap(data, 4096, descriptor.value());
        assert(borrowed);
        borrowed->mutableSpan()[0] = 55;
        assert(memory->span()[0] == 55);
    }
    assert(fcntl(descriptor.value(), F_GETFD) >= 0);
    assert(static_cast<uint8_t*>(data)[0] == 55);
    assert(!SharedMemory::wrapMap(static_cast<uint8_t*>(data) + 1, 4096, descriptor.value()));
    assert(!SharedMemory::wrapMap(nullptr, 4096, descriptor.value()));
    assert(!munmap(data, 4096));
}

static void invalid_inputs()
{
    constexpr size_t maximum_size = 64 * 1024 * 1024;
    assert(!SharedMemory::allocate(0));
    assert(!SharedMemory::allocate(maximum_size + 1));
    assert(!SharedMemory::allocate(SIZE_MAX));
    auto maximum = SharedMemory::allocate(maximum_size);
    assert(maximum && maximum->size() == maximum_size);
    maximum->mutableSpan().back() = 1;
    auto memory = SharedMemory::allocate(4096);
    assert(memory);
    for (uint64_t size : { uint64_t(0), uint64_t(4095), uint64_t(4097), uint64_t(maximum_size + 1), UINT64_MAX }) {
        auto handle = memory->createHandle(Protection::ReadOnly);
        assert(handle);
        SharedMemoryHandle forged(handle->releaseHandle(), size);
        assert(!SharedMemory::map(WTF::move(forged), Protection::ReadOnly));
    }
    int raw = syscall(SYS_memfd_create, "yozora-unsealed-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    assert(raw >= 0 && !ftruncate(raw, 4096));
    SharedMemoryHandle unsealed(UnixFileDescriptor { raw, UnixFileDescriptor::Adopt }, 4096);
    assert(!SharedMemory::map(WTF::move(unsealed), Protection::ReadOnly));
    auto handle = memory->createHandle(Protection::ReadOnly);
    assert(handle);
    SharedMemoryHandle duplicate(*handle);
    auto released = duplicate.releaseHandle();
    assert(!SharedMemory::map(WTF::move(duplicate), Protection::ReadOnly));
    const uint8_t bytes[] = { 0, 1, 255, 42 };
    auto copy = SharedMemoryHandle::createCopy(bytes, Protection::ReadOnly);
    assert(copy);
    auto copied = SharedMemory::map(WTF::move(*copy), Protection::ReadOnly);
    assert(copied && copied->size() == sizeof(bytes));
    assert(!memcmp(copied->span().data(), bytes, sizeof(bytes)));
    auto buffer = SharedBuffer::create(bytes);
    auto buffer_copy = SharedMemory::copyBuffer(buffer.get());
    assert(buffer_copy && !memcmp(buffer_copy->span().data(), bytes, sizeof(bytes)));
    assert(!SharedMemoryHandle::createCopy({ }, Protection::ReadOnly));
}

static void mapping_cleanup()
{
    void* address;
    {
        auto memory = SharedMemory::allocate(4096);
        assert(memory);
        address = memory->mutableSpan().data();
    }
    unsigned char residency;
    assert(mincore(address, 4096, &residency) < 0 && errno == ENOMEM);
}

int main()
{
    WTF::initializeMainThread();
    size_t before = descriptor_count();
    shared_views();
    readonly_views();
    private_views();
    borrowed_views();
    invalid_inputs();
    mapping_cleanup();
    for (unsigned i = 0; i < 128; ++i)
        shared_views();
    assert(descriptor_count() == before);
    std::puts("shared memory: bounds, sealing, readonly rights, sharing, snapshots, borrowed maps and descriptor cleanup passed");
    return 0;
}
