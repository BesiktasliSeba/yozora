#pragma once

#include <atomic>

// while the private session is on, cookies, local storage, databases, stored responses and bytecode of pages
// that start stay in memory only. the flag is read when a page, request or store is made, so the interface
// turns it on before the first private page exists and off once the profile's tabs run again
namespace YozoraPrivateSession {

inline std::atomic<bool>& flag()
{
    static std::atomic<bool> value { false };
    return value;
}
inline bool active() { return flag().load(); }
inline void set(bool on) { flag().store(on); }

}
