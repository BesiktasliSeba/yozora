#include "config.h"
#include "YozoraBytecodeCache.h"

#include "CachedScriptSourceProvider.h"
#include <JavaScriptCore/BytecodeCacheError.h>
#include <JavaScriptCore/CachedBytecode.h>
#include <JavaScriptCore/CachedTypes.h>
#include <JavaScriptCore/HeapCellInlines.h>
#include <JavaScriptCore/UnlinkedFunctionExecutable.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/HashSet.h>
#include <wtf/HexNumber.h>
#include <wtf/Lock.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/WTFSemaphore.h>
#include <wtf/Vector.h>
#include <wtf/WorkQueue.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace WTF::StringLiterals;

namespace YozoraBytecodeCache {

namespace {

// a script this short parses faster than a file opens
constexpr size_t minimumSourceLength = 16 * 1024;
// snapshots on their way to the disk, a slow flash must not let them pile up
constexpr size_t writingLimit = 8 * 1024 * 1024;
// an update that adds less than this to a file is not worth writing the whole file again
constexpr size_t minimumUpdateBytes = 16 * 1024;
constexpr unsigned writesBetweenTrims = 16;

struct State {
    String directory;
    uint64_t maxBytes { 0 };
    size_t pendingLimit { 0 };
    size_t pendingBytes { 0 };
    // snapshots that wait for the writer thread
    size_t writingBytes { 0 };
    unsigned writesSinceTrim { 0 };
    RefPtr<WorkQueue> writer;
    HashSet<const CachedScriptSourceProvider*> providers;
    Statistics statistics;
};

// providers die on whichever thread drops the last reference, the set and the counters need a lock
Lock stateLock;

State& state() WTF_REQUIRES_LOCK(stateLock)
{
    static NeverDestroyed<State> instance;
    return instance.get();
}

bool isCacheFile(const String& name)
{
    return name.endsWith(".bytecode"_s);
}

// the oldest files go first until the directory is back at three quarters of its limit
void trimDirectory(const String& directory, uint64_t maxBytes)
{
    struct Entry {
        String path;
        uint64_t size;
        double modified;
    };
    Vector<Entry> entries;
    uint64_t total = 0;
    for (auto& name : FileSystem::listDirectory(directory)) {
        if (!isCacheFile(name))
            continue;
        String path = FileSystem::pathByAppendingComponent(directory, name);
        auto size = FileSystem::fileSize(path);
        auto modified = FileSystem::fileModificationTime(path);
        if (!size || !modified)
            continue;
        total += *size;
        entries.append({ path, *size, modified->secondsSinceEpoch().value() });
    }
    if (total <= maxBytes)
        return;
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.modified < b.modified; });
    uint64_t target = maxBytes / 4 * 3;
    for (auto& entry : entries) {
        if (total <= target)
            break;
        if (FileSystem::deleteFile(entry.path))
            total -= entry.size;
    }
}

}

bool open(const String& directory, uint64_t maxBytes, size_t pendingLimit)
{
    struct stat info;
    auto path = directory.utf8();
    if (lstat(path.data(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 077)) {
        std::fprintf(stderr, "yozora: bytecode cache directory is not a private directory of this user: %s\n", path.data());
        return false;
    }
    {
        Locker locker { stateLock };
        auto& current = state();
        current.directory = directory;
        current.maxBytes = maxBytes;
        current.pendingLimit = pendingLimit;
    }
    // a write that was cut short by a kill leaves its temporary file behind
    for (auto& name : FileSystem::listDirectory(directory)) {
        if (name.endsWith(".tmp"_s))
            FileSystem::deleteFile(FileSystem::pathByAppendingComponent(directory, name));
    }
    trimDirectory(directory, maxBytes);
    return true;
}

// while the private session runs, scripts are neither loaded from the cache nor kept for it: the file names carry a hash of the site
static std::atomic<bool> privateSession { false };

void setPrivate(bool on)
{
    privateSession.store(on);
}

bool enabled()
{
    if (privateSession.load())
        return false;
    Locker locker { stateLock };
    return !state().directory.isNull();
}

unsigned pending()
{
    Locker locker { stateLock };
    return state().providers.size();
}

void waitForWrites()
{
    RefPtr<WorkQueue> writer;
    {
        Locker locker { stateLock };
        writer = state().writer;
    }
    if (!writer)
        return;
    Semaphore done { 0 };
    writer->dispatch([&done] { done.signal(); });
    done.wait();
}

void flush(unsigned limit)
{
    Vector<const CachedScriptSourceProvider*> due;
    {
        Locker locker { stateLock };
        for (auto* provider : state().providers) {
            if (limit && due.size() >= limit)
                break;
            due.append(provider);
        }
    }
    // a provider that goes away while this runs writes in its destructor first, which takes it out of the set
    for (auto* provider : due)
        provider->commitCachedBytecode();
}

void clear()
{
    String directory;
    {
        Locker locker { stateLock };
        directory = state().directory;
    }
    if (directory.isNull())
        return;
    for (auto& name : FileSystem::listDirectory(directory)) {
        if (isCacheFile(name))
            FileSystem::deleteFile(FileSystem::pathByAppendingComponent(directory, name));
    }
}

Statistics statistics()
{
    Locker locker { stateLock };
    return state().statistics;
}

}

static String cacheDirectory()
{
    Locker locker { YozoraBytecodeCache::stateLock };
    return YozoraBytecodeCache::state().directory;
}

// the file of one script: what it says, where it is served from and how it is compiled. JavaScriptCore
// accepts stored bytecode only for the same host, but path and query are no part of it, sites serve one
// script under a new query string on every visit. the hash of the text's two ends guards against two scripts
// of the same length and hash
String CachedScriptSourceProvider::bytecodePath() const
{
    String directory = cacheDirectory();
    if (directory.isNull())
        return { };
    StringView text = source();
    unsigned length = text.length();
    constexpr unsigned edge = 256;
    uint32_t ends = 2166136261u ^ length;
    for (unsigned i = 0; i < length; ++i) {
        if (i == edge && length > 2 * edge)
            i = length - edge;
        ends = (ends ^ text[i]) * 16777619u;
    }
    return FileSystem::pathByAppendingComponent(directory, makeString(hex(hash(), 8), hex(length, 8), hex(ends, 8), hex(sourceOrigin().url().host().toString().hash(), 8), isModuleType() ? 'm' : 'p', ".bytecode"_s));
}

bool CachedScriptSourceProvider::bytecodeCacheable() const
{
    return YozoraBytecodeCache::enabled() && source().length() >= YozoraBytecodeCache::minimumSourceLength;
}

RefPtr<JSC::CachedBytecode> CachedScriptSourceProvider::cachedBytecode() const
{
    if (m_cachedBytecode || m_bytecodeLoadTried || !bytecodeCacheable())
        return m_cachedBytecode;
    m_bytecodeLoadTried = true;
    String path = bytecodePath();
    if (path.isNull())
        return nullptr;
    auto handle = FileSystem::openFile(path, FileSystem::FileOpenMode::Read);
    if (!handle)
        return nullptr;
    auto mapped = handle.map(FileSystem::MappedFileMode::Private);
    // a file shorter than the header is a write that never finished
    if (!mapped || mapped->size() < 64)
        return nullptr;
    FileSystem::updateFileModificationTime(path);
    m_cachedBytecode = JSC::CachedBytecode::create(WTF::move(*mapped));
    Locker locker { YozoraBytecodeCache::stateLock };
    ++YozoraBytecodeCache::state().statistics.loaded;
    return m_cachedBytecode;
}

void CachedScriptSourceProvider::noteBytecodePending(size_t bytes) const
{
    Locker locker { YozoraBytecodeCache::stateLock };
    auto& current = YozoraBytecodeCache::state();
    current.pendingBytes += bytes;
    m_pendingBytecodeBytes += bytes;
    current.providers.add(this);
}

void CachedScriptSourceProvider::cacheBytecode(const JSC::BytecodeCacheGenerator& generator) const
{
    if (!bytecodeCacheable())
        return;
    {
        Locker locker { YozoraBytecodeCache::stateLock };
        if (YozoraBytecodeCache::state().pendingBytes >= YozoraBytecodeCache::state().pendingLimit)
            return;
    }
    // a file that was there but did not decode is replaced as a whole, appending to it would keep the old header
    if (m_cachedBytecode && m_cachedBytecode->size()) {
        Locker locker { YozoraBytecodeCache::stateLock };
        ++YozoraBytecodeCache::state().statistics.stale;
    }
    m_cachedBytecode = JSC::CachedBytecode::create();
    RefPtr update = generator();
    if (!update) {
        m_cachedBytecode = nullptr;
        return;
    }
    size_t bytes = update->size();
    m_cachedBytecode->addGlobalUpdate(update.releaseNonNull());
    noteBytecodePending(bytes);
}

void CachedScriptSourceProvider::updateCache(const JSC::UnlinkedFunctionExecutable* executable, const JSC::SourceCode&, JSC::CodeSpecializationKind kind, const JSC::UnlinkedFunctionCodeBlock* codeBlock) const
{
    if (!m_cachedBytecode)
        return;
    // a function is added to the file once the code of the function that holds it is there: when that update
    // was left out because pending bytecode memory was full, the function has no place in the file and adding
    // it would read past the table
    if (!m_cachedBytecode->leafExecutables().contains(executable))
        return;
    {
        Locker locker { YozoraBytecodeCache::stateLock };
        if (YozoraBytecodeCache::state().pendingBytes >= YozoraBytecodeCache::state().pendingLimit)
            return;
    }
    JSC::BytecodeCacheError error;
    RefPtr encoded = JSC::encodeFunctionCodeBlock(executable->vm(), codeBlock, error);
    if (!encoded || error.isValid())
        return;
    size_t bytes = encoded->size();
    m_cachedBytecode->addFunctionUpdate(executable, kind, encoded.releaseNonNull());
    noteBytecodePending(bytes);
}

void CachedScriptSourceProvider::commitCachedBytecode() const
{
    if (!m_cachedBytecode || !m_cachedBytecode->hasUpdates())
        return;
    // after this the file holds everything the provider knew, the next lookup maps it again
    Ref<JSC::CachedBytecode> bytecode = *m_cachedBytecode;
    m_cachedBytecode = nullptr;
    m_bytecodeLoadTried = false;
    size_t added = 0;
    {
        Locker locker { YozoraBytecodeCache::stateLock };
        auto& current = YozoraBytecodeCache::state();
        current.providers.remove(this);
        current.pendingBytes -= std::min(current.pendingBytes, m_pendingBytecodeBytes);
        added = m_pendingBytecodeBytes;
        m_pendingBytecodeBytes = 0;
        if (current.writingBytes + bytecode->sizeForUpdate() > YozoraBytecodeCache::writingLimit)
            return;
    }
    // functions that a loaded file lacked and a visit added are written again as a whole file only when they are many
    if (bytecode->span().size() && added < std::max(YozoraBytecodeCache::minimumUpdateBytes, bytecode->span().size() / 20))
        return;
    String path = bytecodePath();
    if (path.isNull())
        return;
    // put together here, where the bytecode is, and written by another thread: the old payload, then the
    // update patches at their offsets. the temporary file is moved over the old one, so a reader with the old
    // file mapped keeps a consistent copy and a cut short write leaves no half file
    Vector<uint8_t> image(bytecode->sizeForUpdate());
    std::span<const uint8_t> base = bytecode->span();
    if (!base.empty())
        memcpySpan(image.mutableSpan(), base);
    bool consistent = true;
    bytecode->commitUpdates([&](off_t offset, std::span<const uint8_t> data) {
        if (offset < 0 || static_cast<size_t>(offset) + data.size() > image.size()) {
            consistent = false;
            return;
        }
        memcpySpan(image.mutableSpan().subspan(offset, data.size()), data);
    });
    if (!consistent)
        return;
    size_t bytes = image.size();
    RefPtr<WorkQueue> writer;
    {
        Locker locker { YozoraBytecodeCache::stateLock };
        auto& current = YozoraBytecodeCache::state();
        if (!current.writer)
            current.writer = WorkQueue::create("org.yozora.bytecodecache"_s);
        writer = current.writer;
        current.writingBytes += bytes;
    }
    writer->dispatch([path = path.isolatedCopy(), image = WTF::move(image), bytes] {
        auto [temporaryPath, handle] = FileSystem::openTemporaryFile("bytecode-"_s, ".tmp"_s, FileSystem::parentPath(path));
        bool written = handle && handle.write(image.span()) == image.size() && handle.flush();
        bool moved = written && FileSystem::moveFile(temporaryPath, path);
        if (!moved && !temporaryPath.isEmpty())
            FileSystem::deleteFile(temporaryPath);
        Locker locker { YozoraBytecodeCache::stateLock };
        auto& current = YozoraBytecodeCache::state();
        current.writingBytes -= std::min(current.writingBytes, bytes);
        if (!moved)
            return;
        ++current.statistics.stored;
        current.statistics.bytes += bytes;
        if (++current.writesSinceTrim >= YozoraBytecodeCache::writesBetweenTrims) {
            current.writesSinceTrim = 0;
            YozoraBytecodeCache::trimDirectory(current.directory, current.maxBytes);
        }
    });
}

void CachedScriptSourceProvider::forgetBytecode()
{
    // work the provider still holds is written while it is alive: later nothing could say which file it belonged to
    if (m_cachedBytecode && m_cachedBytecode->hasUpdates() && isMainThread())
        commitCachedBytecode();
    Locker locker { YozoraBytecodeCache::stateLock };
    auto& current = YozoraBytecodeCache::state();
    current.providers.remove(this);
    current.pendingBytes -= std::min(current.pendingBytes, m_pendingBytecodeBytes);
}

}
