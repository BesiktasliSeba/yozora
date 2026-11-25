#include "config.h"
#include "YozoraCacheStorage.h"

#include "CacheQueryOptions.h"
#include "CacheStorageConnection.h"
#include "CacheStorageProvider.h"
#include "ClientOrigin.h"
#include "CrossOriginAccessControl.h"
#include "DOMCacheEngine.h"
#include "HTTPHeaderNames.h"
#include "OriginAccessPatterns.h"
#include "ResourceError.h"
#include "ResourceResponse.h"
#include "RetrieveRecordsOptions.h"
#include "YozoraPrivateSession.h"
#include <wtf/CheckedArithmetic.h>
#include <wtf/HashMap.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/MakeString.h>

// the cache storage api: window.caches, and self.caches in workers. WebKit serves it from the network process of its
// Cocoa and GTK ports (WebKit/NetworkProcess/storage: CacheStorageManager, CacheStorageCache), which a port of one
// process does not have, so pages found no caches object and a site that opens a cache while its storage starts
// stopped there (web.whatsapp.com: "Can't find variable: caches", storage_initialization_error). this is that engine
// with the memory store WebKit keeps for its ephemeral sessions, on the core thread that owns WebCore: caches per
// origin in the order they were opened, records per cache in the order they were put, and the rules of
// CacheStorageCache for a match, a put over an existing record, a delete and the Cross-Origin-Resource-Policy check
// of an opaque response. nothing goes to disk, a cache lasts for the life of the app, which the specification allows
// because a user agent may evict a cache at any time

using namespace WebCore;
using namespace WTF::StringLiterals;
namespace CE = WebCore::DOMCacheEngine;

namespace YozoraCacheStorage {

namespace {

// the bodies of every cache of one engine together. they stay in memory, and a site must not grow the app without
// bound on a phone with 512 MB
constexpr uint64_t quotaBytes = 32 * 1024 * 1024;

struct Cache {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(Cache);
    Cache(DOMCacheIdentifier cacheIdentifier, String&& cacheOrigin, String&& cacheName)
        : identifier(cacheIdentifier), origin(WTF::move(cacheOrigin)), name(WTF::move(cacheName)) { }
    DOMCacheIdentifier identifier;
    String origin;
    String name;
    Vector<CE::Record> records;
    // a cache that caches.delete() removed stays usable for the Cache objects that still hold it, as in WebKit
    bool removed { false };
    unsigned references { 0 };
};

class Engine final : public CacheStorageConnection {
public:
    static Ref<Engine> create() { return adoptRef(*new Engine); }

    void clear()
    {
        ASSERT(isMainThread());
        for (auto& cache : m_caches) {
            if (!cache->removed)
                bump(cache->origin);
            cache->removed = true;
            cache->records.clear();
        }
        m_caches.removeAllMatching([](auto& cache) { return !cache->references; });
        m_size = 0;
    }

private:
    Engine() = default;

    static String originKey(const ClientOrigin& origin)
    {
        // a space is in no serialized origin, so two different pairs of origins never make the same key
        return makeString(origin.topOrigin.toString(), ' ', origin.clientOrigin.toString());
    }
    void bump(const String& origin) { ++m_updateCounters.add(origin, 0).iterator->value; }
    Cache* find(DOMCacheIdentifier identifier)
    {
        for (auto& cache : m_caches) {
            if (cache->identifier == identifier)
                return cache.get();
        }
        return nullptr;
    }
    // the record a put replaces, found as CacheStorageCache::findExistingRecord finds it: the first match with the
    // default options
    static CE::Record* findExisting(Cache& cache, const ResourceRequest& request)
    {
        for (auto& record : cache.records) {
            if (CE::queryCacheMatch(request, record.request, record.response, CacheQueryOptions { }))
                return &record;
        }
        return nullptr;
    }
    // the answer of open is made where the promise is read, as the IPC of WebKit makes its replies
    // (Connection::sendWithPromisedReply): a resolve() copies its value for another thread, and a
    // CacheIdentifierOperationResult has no isolatedCopy
    static Ref<OpenPromise> opened(DOMCacheIdentifier identifier)
    {
        OpenPromise::Producer producer;
        producer.settleWithFunction([identifier] { return OpenPromise::Result { CE::CacheIdentifierOperationResult { identifier, false } }; });
        return producer.promise();
    }
    void release(size_t index)
    {
        for (auto& record : m_caches[index]->records)
            m_size -= std::min(m_size, record.responseBodySize);
        m_caches.removeAt(index);
    }

    Ref<OpenPromise> open(const ClientOrigin& clientOrigin, const String& name) final
    {
        ASSERT(isMainThread());
        auto origin = originKey(clientOrigin);
        for (auto& cache : m_caches) {
            if (!cache->removed && cache->origin == origin && cache->name == name)
                return opened(cache->identifier);
        }
        auto identifier = DOMCacheIdentifier::generate();
        bump(origin);
        m_caches.append(makeUnique<Cache>(identifier, WTF::move(origin), String { name }));
        return opened(identifier);
    }

    Ref<RemovePromise> remove(DOMCacheIdentifier identifier) final
    {
        ASSERT(isMainThread());
        auto index = m_caches.findIf([&](auto& cache) { return cache->identifier == identifier && !cache->removed; });
        if (index == notFound)
            return RemovePromise::createAndResolve(false);
        auto& cache = *m_caches[index];
        cache.removed = true;
        bump(cache.origin);
        if (!cache.references)
            release(index);
        return RemovePromise::createAndResolve(true);
    }

    Ref<RetrieveCachesPromise> retrieveCaches(const ClientOrigin& clientOrigin, uint64_t) final
    {
        ASSERT(isMainThread());
        auto origin = originKey(clientOrigin);
        Vector<CE::CacheInfo> infos;
        for (auto& cache : m_caches) {
            if (!cache->removed && cache->origin == origin)
                infos.append(CE::CacheInfo { cache->identifier, cache->name });
        }
        return RetrieveCachesPromise::createAndResolve(CE::CacheInfos { WTF::move(infos), m_updateCounters.get(origin) });
    }

    // the steps of CacheStorageCache::findRecords and retrieveRecords, so a match answers as on the WebKit ports
    Ref<RetrieveRecordsPromise> retrieveRecords(DOMCacheIdentifier identifier, RetrieveRecordsOptions&& options) final
    {
        ASSERT(isMainThread());
        auto* cache = find(identifier);
        if (!cache)
            return RetrieveRecordsPromise::createAndReject(CE::Error::Internal);
        bool all = options.request.url().isNull();
        if (!all && !options.ignoreMethod && options.request.httpMethod() != "GET"_s)
            return RetrieveRecordsPromise::createAndResolve(Vector<CE::CrossThreadRecord> { });
        CacheQueryOptions queryOptions { options.ignoreSearch, options.ignoreMethod, options.ignoreVary };
        Vector<CE::CrossThreadRecord> result;
        for (auto& stored : cache->records) {
            if (!all && !CE::queryCacheMatch(options.request, stored.request, stored.response, queryOptions))
                continue;
            auto record = CE::toCrossThreadRecord(stored.copy());
            record.updateResponseCounter = 0;
            if (!options.shouldProvideResponse) {
                record.response = ResourceResponse::CrossThreadData { };
                record.responseBody = nullptr;
                record.responseBodySize = 0;
            }
            if (record.response.type == ResourceResponse::Type::Opaque) {
                if (validateCrossOriginResourcePolicy(options.crossOriginEmbedderPolicy.value, options.sourceOrigin, record.request.url(), false, record.response.url, record.response.httpHeaderFields.get(HTTPHeaderName::CrossOriginResourcePolicy), ForNavigation::No, EmptyOriginAccessPatterns::singleton()))
                    return RetrieveRecordsPromise::createAndReject(CE::Error::CORP);
            }
            result.append(WTF::move(record));
        }
        return RetrieveRecordsPromise::createAndResolve(WTF::move(result));
    }

    // the steps of CacheStorageCache::removeRecords, so a delete takes what a match with the same options finds
    Ref<BatchPromise> batchDeleteOperation(DOMCacheIdentifier identifier, const ResourceRequest& request, CacheQueryOptions&& options) final
    {
        ASSERT(isMainThread());
        auto* cache = find(identifier);
        if (!cache)
            return BatchPromise::createAndReject(CE::Error::Internal);
        Vector<uint64_t> removed;
        if (!options.ignoreMethod && request.httpMethod() != "GET"_s)
            return BatchPromise::createAndResolve(WTF::move(removed));
        cache->records.removeAllMatching([&](auto& record) {
            if (!CE::queryCacheMatch(request, record.request, record.response, options))
                return false;
            removed.append(record.identifier);
            m_size -= std::min(m_size, record.responseBodySize);
            return true;
        });
        return BatchPromise::createAndResolve(WTF::move(removed));
    }

    // the steps of CacheStorageCache::putRecords: the quota is checked for the whole batch first, so a batch that does
    // not fit leaves the cache as it was, then a record that matches an existing one takes its place and keeps its
    // identifier and request
    Ref<BatchPromise> batchPutOperation(DOMCacheIdentifier identifier, Vector<CE::CrossThreadRecord>&& records) final
    {
        ASSERT(isMainThread());
        auto* cache = find(identifier);
        if (!cache)
            return BatchPromise::createAndReject(CE::Error::Internal);
        CheckedUint64 requested = 0;
        uint64_t released = 0;
        for (auto& record : records) {
            requested += record.responseBodySize;
            if (auto* existing = findExisting(*cache, record.request))
                released += existing->responseBodySize;
        }
        // m_size never passes the quota, so the room left cannot wrap, and a batch is compared with it instead of
        // being added to m_size, which a body size near 2^64 would overflow
        uint64_t kept = m_size - std::min(m_size, released);
        if (requested.hasOverflowed() || requested.value() > quotaBytes - kept)
            return BatchPromise::createAndReject(CE::Error::QuotaExceeded);
        Vector<uint64_t> identifiers;
        for (auto& crossThreadRecord : records) {
            auto record = CE::fromCrossThreadRecord(WTF::move(crossThreadRecord));
            if (auto* existing = findExisting(*cache, record.request)) {
                m_size -= std::min(m_size, existing->responseBodySize);
                existing->responseHeadersGuard = record.responseHeadersGuard;
                existing->response = WTF::move(record.response);
                existing->responseBody = WTF::move(record.responseBody);
                existing->responseBodySize = record.responseBodySize;
                m_size += existing->responseBodySize;
                identifiers.append(existing->identifier);
                continue;
            }
            record.identifier = ++m_lastRecordIdentifier;
            m_size += record.responseBodySize;
            identifiers.append(record.identifier);
            cache->records.append(WTF::move(record));
        }
        return BatchPromise::createAndResolve(WTF::move(identifiers));
    }

    void reference(DOMCacheIdentifier identifier) final
    {
        ASSERT(isMainThread());
        if (auto* cache = find(identifier))
            ++cache->references;
    }
    void dereference(DOMCacheIdentifier identifier) final
    {
        ASSERT(isMainThread());
        auto index = m_caches.findIf([&](auto& cache) { return cache->identifier == identifier; });
        if (index == notFound)
            return;
        auto& cache = *m_caches[index];
        if (cache.references)
            --cache.references;
        if (!cache.references && cache.removed)
            release(index);
    }
    // one process and one thread: nothing to hold for a page while it reads the list
    void lockStorage(const ClientOrigin&) final { }
    void unlockStorage(const ClientOrigin&) final { }

    Vector<std::unique_ptr<Cache>> m_caches;
    HashMap<String, uint64_t> m_updateCounters;
    uint64_t m_lastRecordIdentifier { 0 };
    uint64_t m_size { 0 };
};

class Provider final : public CacheStorageProvider {
public:
    static Ref<Provider> create(Ref<Engine>&& engine) { return adoptRef(*new Provider(WTF::move(engine))); }
private:
    explicit Provider(Ref<Engine>&& engine) : m_engine(WTF::move(engine)) { }
    // every page and worker of a session shares the one engine, as every page of a WebKit process shares its connection
    Ref<CacheStorageConnection> createCacheStorageConnection() final { return m_engine.copyRef(); }
    Ref<Engine> m_engine;
};

Ref<Engine>& engine(bool isPrivate)
{
    static NeverDestroyed<Ref<Engine>> profile { Engine::create() };
    static NeverDestroyed<Ref<Engine>> session { Engine::create() };
    return isPrivate ? session.get() : profile.get();
}

}

Ref<CacheStorageProvider> provider()
{
    // a page made while the private session runs keeps its caches apart from the profile's, clearPrivate empties them
    // with the rest of the session
    bool isPrivate = YozoraPrivateSession::active();
    static NeverDestroyed<Ref<Provider>> profile { Provider::create(engine(false).copyRef()) };
    static NeverDestroyed<Ref<Provider>> session { Provider::create(engine(true).copyRef()) };
    return isPrivate ? session.get().copyRef() : profile.get().copyRef();
}

void clear()
{
    engine(false)->clear();
}

void clearPrivate()
{
    engine(true)->clear();
}

}
