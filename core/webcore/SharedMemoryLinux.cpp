#include "config.h"
#include "SharedMemory.h"
#include "../darwin/yozora_memory.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <wtf/RefPtr.h>
#include <wtf/SafeStrerror.h>
#include <wtf/text/CString.h>

namespace WebCore {

static constexpr uint64_t maximum_memory_size = YOZORA_MEMORY_MAX_SIZE;
static constexpr int required_seals = F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;

static bool memory_failure(const char* operation)
{
    WTFLogAlways("yozora shared memory: %s: %s", operation, safeStrerror(errno).data());
    return false;
}

static bool valid_memory(int descriptor, uint64_t size)
{
    if (!size || size > maximum_memory_size) {
        errno = EINVAL;
        return memory_failure("invalid size");
    }
    struct stat status;
    if (fstat(descriptor, &status) < 0)
        return memory_failure("inspect descriptor");
    if (!S_ISREG(status.st_mode) || status.st_size < 0 || static_cast<uint64_t>(status.st_size) != size) {
        errno = EINVAL;
        return memory_failure("backing size mismatch");
    }
    int seals = fcntl(descriptor, F_GET_SEALS);
    if (seals < 0 || (seals & required_seals) != required_seals) {
        errno = EINVAL;
        return memory_failure("unsealed backing");
    }
    return true;
}

SharedMemoryHandle::SharedMemoryHandle(const SharedMemoryHandle& handle)
    : m_handle(handle.m_handle.duplicate())
    , m_size(handle.m_size)
{
    if (!m_handle)
        memory_failure("copy handle");
}

UnixFileDescriptor SharedMemoryHandle::releaseHandle()
{
    return WTF::move(m_handle);
}

RefPtr<SharedMemory> SharedMemory::allocate(size_t size)
{
    if (!size || size > maximum_memory_size) {
        errno = EINVAL;
        memory_failure("allocation size");
        return nullptr;
    }
    int descriptor;
    do {
        descriptor = syscall(SYS_memfd_create, "yozora-memory", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    } while (descriptor < 0 && errno == EINTR);
    UnixFileDescriptor backing { descriptor, UnixFileDescriptor::Adopt };
    if (!backing) {
        memory_failure("create backing");
        return nullptr;
    }
    int result;
    do {
        result = ftruncate(backing.value(), size);
    } while (result < 0 && errno == EINTR);
    if (result < 0 || fcntl(backing.value(), F_ADD_SEALS, required_seals) < 0) {
        memory_failure("fix backing size");
        return nullptr;
    }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, backing.value(), 0);
    if (data == MAP_FAILED) {
        memory_failure("allocate mapping");
        return nullptr;
    }
    RefPtr<SharedMemory> memory = adoptRef(new SharedMemory);
    memory->m_size = size;
    memory->m_data = data;
    memory->m_fileDescriptor = WTF::move(backing);
    return memory;
}

RefPtr<SharedMemory> SharedMemory::map(Handle&& handle, Protection protection, CopyOnWrite copy_on_write)
{
    if (!valid_memory(handle.m_handle.value(), handle.m_size))
        return nullptr;
    int access = PROT_READ | (protection == Protection::ReadWrite ? PROT_WRITE : 0);
    int flags = copy_on_write == CopyOnWrite::Yes ? MAP_PRIVATE : MAP_SHARED;
    void* data = mmap(nullptr, handle.m_size, access, flags, handle.m_handle.value(), 0);
    if (data == MAP_FAILED) {
        memory_failure("import mapping");
        return nullptr;
    }
    RefPtr<SharedMemory> memory = adoptRef(new SharedMemory);
    memory->m_size = handle.m_size;
    memory->m_data = data;
    memory->m_fileDescriptor = WTF::move(handle.m_handle);
    memory->m_isCopyOnWrite = copy_on_write == CopyOnWrite::Yes;
    return memory;
}

RefPtr<SharedMemory> SharedMemory::wrapMap(void* data, size_t size, int descriptor)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (!data || page_size <= 0 || reinterpret_cast<uintptr_t>(data) % page_size) {
        errno = EINVAL;
        memory_failure("unaligned borrowed mapping");
        return nullptr;
    }
    if (!valid_memory(descriptor, size))
        return nullptr;
    RefPtr<SharedMemory> memory = adoptRef(new SharedMemory);
    memory->m_size = size;
    memory->m_data = data;
    memory->m_fileDescriptor = UnixFileDescriptor { descriptor, UnixFileDescriptor::Borrow };
    memory->m_isWrappingMap = true;
    return memory;
}

SharedMemory::~SharedMemory()
{
    if (!m_isWrappingMap && munmap(m_data, m_size) < 0)
        memory_failure("release mapping");
}

auto SharedMemory::createHandle(Protection protection) -> std::optional<Handle>
{
    // a private mapping must export its changed bytes rather than its original backing
    if (m_isCopyOnWrite)
        return Handle::createCopy(span(), protection);
    UnixFileDescriptor descriptor;
    if (protection == Protection::ReadOnly) {
        char path[64];
        int length = snprintf(path, sizeof(path), "/proc/self/fd/%d", m_fileDescriptor.value());
        if (length < 0 || static_cast<size_t>(length) >= sizeof(path)) {
            errno = EOVERFLOW;
            memory_failure("descriptor path");
            return std::nullopt;
        }
        int opened;
        do {
            opened = open(path, O_RDONLY | O_CLOEXEC);
        } while (opened < 0 && errno == EINTR);
        descriptor = UnixFileDescriptor { opened, UnixFileDescriptor::Adopt };
    } else {
        int access = fcntl(m_fileDescriptor.value(), F_GETFL);
        if (access < 0 || (access & O_ACCMODE) != O_RDWR) {
            errno = EACCES;
            memory_failure("write access denied");
            return std::nullopt;
        }
        descriptor = m_fileDescriptor.duplicate();
    }
    if (!descriptor) {
        memory_failure("export handle");
        return std::nullopt;
    }
    return Handle(WTF::move(descriptor), m_size);
}

}
