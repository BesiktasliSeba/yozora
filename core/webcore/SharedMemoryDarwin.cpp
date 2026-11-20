#include "config.h"
#include "SharedMemory.h"
#include "../darwin/yozora_memory.h"
#include <wtf/RefPtr.h>

namespace WebCore {

static void memory_failure(const char* operation, yozora_memory_status status, int32_t kernel_error)
{
    WTFLogAlways("yozora shared memory: %s failed (status %d, Mach %d)", operation, status, kernel_error);
}

std::optional<SharedMemoryHandle> SharedMemoryHandle::createVMShare(std::span<const uint8_t> data, SharedMemoryProtection protection)
{
    uint32_t port = 0;
    int32_t error = 0;
    auto status = yozora_memory_share(const_cast<uint8_t*>(data.data()), data.size(), protection == SharedMemoryProtection::ReadWrite, &port, &error);
    if (status != YOZORA_MEMORY_OK) {
        memory_failure("share VM", status, error);
        return std::nullopt;
    }
    return SharedMemoryHandle(MachSendRight::adopt(port), data.size());
}

std::optional<SharedMemoryHandle> SharedMemoryHandle::createVMCopy(std::span<const uint8_t> data, SharedMemoryProtection protection)
{
    auto source = createVMShare(data, protection);
    if (!source)
        return std::nullopt;
    auto snapshot = SharedMemory::map(WTF::move(*source), protection, SharedMemory::CopyOnWrite::Yes);
    if (!snapshot)
        return std::nullopt;
    return createVMShare(snapshot->span(), protection);
}

RefPtr<SharedMemory> SharedMemory::allocate(size_t size)
{
    void* data = nullptr;
    uint32_t port = 0;
    int32_t error = 0;
    auto status = yozora_memory_allocate(size, &data, &port, &error);
    if (status != YOZORA_MEMORY_OK) {
        memory_failure("allocate", status, error);
        return nullptr;
    }
    RefPtr memory = adoptRef(new SharedMemory);
    memory->m_size = size;
    memory->m_data = data;
    memory->m_sendRight = MachSendRight::adopt(port);
    return memory;
}

RefPtr<SharedMemory> SharedMemory::map(Handle&& handle, Protection protection, CopyOnWrite copy_on_write)
{
    void* data = nullptr;
    int32_t error = 0;
    auto status = yozora_memory_map(handle.m_handle.sendRight(), handle.m_size, protection == Protection::ReadWrite,
        copy_on_write == CopyOnWrite::Yes, &data, &error);
    if (status != YOZORA_MEMORY_OK) {
        memory_failure("map", status, error);
        return nullptr;
    }
    RefPtr memory = adoptRef(new SharedMemory);
    memory->m_size = static_cast<size_t>(handle.m_size);
    memory->m_data = data;
    memory->m_sendRight = WTF::move(handle.m_handle);
    memory->m_protection = protection;
    memory->m_isCopyOnWrite = copy_on_write == CopyOnWrite::Yes;
    return memory;
}

RefPtr<SharedMemory> SharedMemory::wrapMap(std::span<const uint8_t> data, Protection protection)
{
    uint32_t port = 0;
    int32_t error = 0;
    auto status = yozora_memory_share(const_cast<uint8_t*>(data.data()), data.size(), protection == Protection::ReadWrite, &port, &error);
    if (status != YOZORA_MEMORY_OK) {
        memory_failure("wrap mapping", status, error);
        return nullptr;
    }
    RefPtr memory = adoptRef(new SharedMemory);
    memory->m_size = data.size();
    memory->m_data = const_cast<uint8_t*>(data.data());
    memory->m_sendRight = MachSendRight::adopt(port);
    memory->m_protection = protection;
    memory->m_isWrappingMap = true;
    return memory;
}

SharedMemory::~SharedMemory()
{
    if (m_isWrappingMap)
        return;
    int32_t error = 0;
    auto status = yozora_memory_deallocate(m_data, m_size, &error);
    if (status != YOZORA_MEMORY_OK)
        memory_failure("deallocate", status, error);
}

MachSendRight SharedMemory::createSendRight(Protection protection) const
{
    uint32_t port = 0;
    int32_t error = 0;
    auto status = yozora_memory_export(m_sendRight.sendRight(), m_size, protection == Protection::ReadWrite, &port, &error);
    if (status != YOZORA_MEMORY_OK) {
        memory_failure("export", status, error);
        return { };
    }
    return MachSendRight::adopt(port);
}

auto SharedMemory::createHandle(Protection protection) -> std::optional<Handle>
{
    if (protection == Protection::ReadWrite && m_protection == Protection::ReadOnly) {
        memory_failure("write access denied", YOZORA_MEMORY_ACCESS, 0);
        return std::nullopt;
    }
    // a private view must export its changed bytes rather than the original memory entry
    if (m_isCopyOnWrite)
        return Handle::createCopy(span(), protection);
    auto right = createSendRight(protection);
    if (!right)
        return std::nullopt;
    return Handle(WTF::move(right), m_size);
}

}
