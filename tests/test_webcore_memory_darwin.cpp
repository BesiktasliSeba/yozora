#include "config.h"
#include "SharedMemory.h"
#include <cstdio>
#include <mach/mach.h>
#include <wtf/MainThread.h>
#include <wtf/RefPtr.h>

// release WTF omits debug assertion helpers, enable C assertions after its headers
#undef NDEBUG
#include <cassert>

using namespace WebCore;
using Protection = SharedMemory::Protection;

static void memory_roundtrip()
{
    auto owner = SharedMemory::allocate(vm_page_size);
    assert(owner);
    owner->mutableSpan()[17] = 42;
    auto handle = owner->createHandle(Protection::ReadWrite);
    assert(handle);
    SharedMemoryHandle duplicate(*handle);
    auto view = SharedMemory::map(WTF::move(duplicate), Protection::ReadWrite);
    assert(view && view->span()[17] == 42);
    view->mutableSpan()[18] = 99;
    assert(owner->span()[18] == 99);
    auto ro_handle = owner->createHandle(Protection::ReadOnly);
    assert(ro_handle);
    auto readonly = SharedMemory::map(WTF::move(*ro_handle), Protection::ReadOnly);
    assert(readonly && readonly->span()[17] == 42);
    assert(!readonly->createHandle(Protection::ReadWrite));
    assert(vm_protect(mach_task_self(), reinterpret_cast<vm_address_t>(readonly->span().data()), vm_page_size,
        FALSE, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS);
    auto copy = SharedMemory::map(WTF::move(*handle), Protection::ReadWrite, SharedMemory::CopyOnWrite::Yes);
    assert(copy);
    copy->mutableSpan()[17] = 11;
    assert(owner->span()[17] == 42);
    auto exported = copy->createHandle(Protection::ReadOnly);
    assert(exported);
    copy = nullptr;
    auto copied = SharedMemory::map(WTF::move(*exported), Protection::ReadOnly);
    assert(copied && copied->span()[17] == 11);
    auto borrowed = SharedMemory::wrapMap(owner->span(), Protection::ReadOnly);
    assert(borrowed && borrowed->span()[17] == 42);
    borrowed = nullptr;
    owner->mutableSpan()[17] = 77;
    assert(view->span()[17] == 77 && readonly->span()[17] == 77 && copied->span()[17] == 11);
    auto snapshot = SharedMemoryHandle::createVMCopy(owner->span(), Protection::ReadOnly);
    assert(snapshot);
    auto snapshot_view = SharedMemory::map(WTF::move(*snapshot), Protection::ReadOnly);
    assert(snapshot_view && snapshot_view->span()[17] == 77);
    owner->mutableSpan()[17] = 88;
    assert(snapshot_view->span()[17] == 77);
    owner = nullptr;
    assert(view->span()[17] == 88);
}

static void send_right_lifetime()
{
    mach_port_t receive = MACH_PORT_NULL;
    assert(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &receive) == KERN_SUCCESS);
    auto right = MachSendRight::createFromReceiveRight(receive);
    assert(right);
    {
        MachSendRight duplicate(right);
        mach_port_urefs_t refs = 0;
        assert(mach_port_get_refs(mach_task_self(), receive, MACH_PORT_RIGHT_SEND, &refs) == KERN_SUCCESS && refs == 2);
        auto moved = WTF::move(duplicate);
        assert(moved && !duplicate);
        duplicate = WTF::move(moved);
        assert(duplicate && !moved);
    }
    assert(mach_port_mod_refs(mach_task_self(), receive, MACH_PORT_RIGHT_RECEIVE, -1) == KERN_SUCCESS);
    MachSendRight dead_copy(right);
    mach_port_urefs_t refs = 0;
    assert(mach_port_get_refs(mach_task_self(), receive, MACH_PORT_RIGHT_DEAD_NAME, &refs) == KERN_SUCCESS && refs == 2);
}

int main()
{
    WTF::initializeMainThread();
    memory_roundtrip();
    send_right_lifetime();
    assert(!SharedMemory::allocate(0));
    assert(!SharedMemory::allocate(64 * 1024 * 1024 + 1));
    assert(std::puts("webcore Darwin memory: sharing, rights, snapshots, borrowed maps and Mach right ownership passed") >= 0);
    return 0;
}
