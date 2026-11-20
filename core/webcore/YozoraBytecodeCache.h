#pragma once

#include <wtf/text/WTFString.h>

namespace WebCore {

// bytecode of external scripts kept on disk, a script loaded again skips the parser and bytecode generator.
// every call runs on the core thread
namespace YozoraBytecodeCache {

// the directory must exist, belong to the caller and have mode 0700, false when unusable. pendingLimit bounds
// the bytecode pages keep in memory until written
bool open(const String& directory, uint64_t maxBytes, size_t pendingLimit);
bool enabled();
// turns the cache off for as long as the private session runs, the files already stored stay
void setPrivate(bool on);
// writes what scripts gathered since they were last written, at most limit of them, 0 for all
void flush(unsigned limit);
unsigned pending();
// returns when everything handed to the writer thread is on disk, for the moment the app leaves the foreground
void waitForWrites();
void clear();

struct Statistics {
    unsigned long long loaded { 0 };
    unsigned long long stale { 0 };
    unsigned long long stored { 0 };
    unsigned long long bytes { 0 };
};
Statistics statistics();

}

}
