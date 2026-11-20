#include "config.h"
#include <wtf/MachSendRight.h>

#include <mach/mach.h>
#include <utility>

namespace WTF {

static void require_port_result(const char* operation, kern_return_t result)
{
    if (result == KERN_SUCCESS)
        return;
    WTFLogAlways("yozora Mach right: %s failed (%d)", operation, result);
    RELEASE_ASSERT_NOT_REACHED();
}

void deallocateSendRightSafely(mach_port_t port)
{
    if (port != MACH_PORT_NULL)
        require_port_result("deallocate", mach_port_deallocate(mach_task_self(), port));
}

MachSendRight MachSendRight::adopt(mach_port_t port)
{
    if (port != MACH_PORT_NULL) {
        mach_port_type_t type = 0;
        require_port_result("inspect", mach_port_type(mach_task_self(), port, &type));
        RELEASE_ASSERT(type & (MACH_PORT_TYPE_SEND | MACH_PORT_TYPE_DEAD_NAME));
    }
    return MachSendRight(port);
}

MachSendRight MachSendRight::create(mach_port_t port)
{
    if (port == MACH_PORT_NULL)
        return { };
    kern_return_t result = mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, 1);
    // a receive right may die before its surviving send right is copied
    if (result == KERN_INVALID_RIGHT)
        result = mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_DEAD_NAME, 1);
    require_port_result("retain", result);
    return adopt(port);
}

MachSendRight MachSendRight::createFromReceiveRight(mach_port_t receive_right)
{
    auto result = mach_port_insert_right(mach_task_self(), receive_right, receive_right, MACH_MSG_TYPE_MAKE_SEND);
    if (result != KERN_SUCCESS) {
        WTFLogAlways("yozora Mach right: create send right failed (%d)", result);
        return { };
    }
    return adopt(receive_right);
}

MachSendRight::MachSendRight(mach_port_t port)
    : m_port(port)
{
}

MachSendRight::MachSendRight(const MachSendRight& other)
    : MachSendRight(create(other.m_port))
{
}

MachSendRight::MachSendRight(MachSendRight&& other)
    : m_port(other.leakSendRight())
{
}

MachSendRight& MachSendRight::operator=(MachSendRight&& other)
{
    if (this == &other)
        return *this;
    auto previous = std::exchange(m_port, other.leakSendRight());
    deallocateSendRightSafely(previous);
    return *this;
}

MachSendRight::~MachSendRight()
{
    deallocateSendRightSafely(m_port);
}

mach_port_t MachSendRight::leakSendRight()
{
    return std::exchange(m_port, MACH_PORT_NULL);
}

}
