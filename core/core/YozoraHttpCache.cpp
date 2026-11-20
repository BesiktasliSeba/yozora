#include "config.h"
#include <pthread.h>
#include "YozoraHttpCache.h"
#include "YozoraPrivateSession.h"
#include "HTTPHeaderNames.h"
#include "HTTPParsers.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include <array>
#include <cstdio>
#include <wtf/WTFSemaphore.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/WallTime.h>
#include <wtf/WorkQueue.h>
#include <wtf/MainThread.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace WTF::StringLiterals;

namespace YozoraHttpCache {

namespace {

// the cache is touched only by the queue after open() returns
struct Service {
    yozora_httpcache* cache { nullptr };
    RefPtr<WorkQueue> queue;
    // responses wait in memory for the thread, a slow flash must not let them pile up past a few megabytes
    yozora_byte_budget pending { 0, 0 };
};

constexpr size_t pendingLimit = 4 * 1024 * 1024;

Service& service()
{
    static NeverDestroyed<Service> instance;
    return instance.get();
}

// created on first use: a work queue made before the core starts the main thread machinery trips its
// initialization. the interface thread flushes and clears while the core thread looks up and stores
WorkQueue& queue()
{
    static pthread_mutex_t creation = PTHREAD_MUTEX_INITIALIZER;
    auto& state = service();
    pthread_mutex_lock(&creation);
    if (!state.queue)
        state.queue = WorkQueue::create("org.yozora.httpcache"_s);
    WorkQueue& result = *state.queue;
    pthread_mutex_unlock(&creation);
    return result;
}

// headers that describe one transfer or one user, a replay must not carry them
bool isTransferHeader(const String& name)
{
    static constexpr std::array<ASCIILiteral, 14> excluded {
        "content-length"_s, "content-encoding"_s, "content-range"_s, "transfer-encoding"_s, "connection"_s,
        "keep-alive"_s, "set-cookie"_s, "set-cookie2"_s, "proxy-authenticate"_s, "proxy-authorization"_s,
        "upgrade"_s, "te"_s, "trailer"_s, "www-authenticate"_s
    };
    for (auto& entry : excluded) {
        if (equalIgnoringASCIICase(name, entry))
            return true;
    }
    return false;
}

using HeaderList = Vector<std::pair<String, String>>;

HeaderList parseBlock(const Vector<char>& block)
{
    HeaderList headers;
    String text = String::fromUTF8WithLatin1Fallback(block.span());
    for (auto line : text.splitAllowingEmptyEntries('\n')) {
        auto trimmed = line.trim(deprecatedIsSpaceOrNewline);
        auto colon = trimmed.find(':');
        if (colon == notFound || !colon)
            continue;
        headers.append({ trimmed.left(colon).trim(deprecatedIsSpaceOrNewline), trimmed.substring(colon + 1).trim(deprecatedIsSpaceOrNewline) });
    }
    return headers;
}

bool serialize(const HeaderList& headers, Vector<char>& block)
{
    block.clear();
    for (auto& [name, value] : headers) {
        // a line break inside a value would forge another header on the next read
        if (name.contains('\n') || name.contains('\r') || name.contains(':') || value.contains('\n') || value.contains('\r'))
            continue;
        auto line = makeString(name, ": "_s, value, "\r\n"_s).utf8();
        if (block.size() + line.length() > YOZORA_HTTPCACHE_HEADERS_CAP)
            return false;
        block.append(line.span());
    }
    return true;
}

HeaderList responseHeaders(const ResourceResponse& response)
{
    HeaderList headers;
    for (auto& header : response.httpHeaderFields()) {
        if (!isTransferHeader(header.key))
            headers.append({ header.key, header.value });
    }
    return headers;
}

int64_t nowMilliseconds()
{
    return static_cast<int64_t>(WallTime::now().secondsSinceEpoch().milliseconds());
}

}

bool open(const char* directory, uint64_t max_bytes, char* error, size_t capacity)
{
    auto& state = service();
    if (state.cache) {
        std::snprintf(error, capacity, "Кэш уже открыт");
        return false;
    }
    yozora_byte_budget_init(&state.pending, pendingLimit);
    yozora_status status = yozora_httpcache_open(&state.cache, directory, max_bytes);
    if (status != YOZORA_OK) {
        std::snprintf(error, capacity, "Не удалось открыть кэш: %s", yozora_status_string(status));
        return false;
    }
    return true;
}

bool enabled()
{
    // a page of the private session neither reads nor leaves a stored response
    return !!service().cache && !YozoraPrivateSession::active();
}

void clear()
{
    auto& state = service();
    if (!state.cache)
        return;
    queue().dispatch([cache = state.cache] {
        yozora_status status = yozora_httpcache_clear(cache);
        if (status != YOZORA_OK)
            std::fprintf(stderr, "yozora: http cache clear failed: %s\n", yozora_status_string(status));
    });
}

void flush()
{
    if (!service().cache)
        return;
    Semaphore done { 0 };
    queue().dispatch([&done] { done.signal(); });
    done.wait();
}

void lookup(const String& url, Function<void(std::optional<CachedResponse>&&)>&& completion)
{
    auto& state = service();
    ASSERT(state.cache);
    queue().dispatch([cache = state.cache, url = url.isolatedCopy(), completion = WTF::move(completion)]() mutable {
        std::optional<CachedResponse> result;
        auto text = url.utf8();
        yozora_cache_entry entry;
        yozora_status status = yozora_httpcache_load(cache, text.data(), &entry);
        if (status == YOZORA_OK) {
            CachedResponse response;
            response.response_ms = entry.response_ms;
            response.headers.append(std::span { entry.headers, entry.headers_length });
            response.body.append(std::span { entry.body, entry.body_length });
            yozora_cache_entry_release(&entry);
            result = WTF::move(response);
        } else if (status != YOZORA_NOT_FOUND)
            std::fprintf(stderr, "yozora: http cache read dropped an entry: %s\n", yozora_status_string(status));
        callOnMainThread([completion = WTF::move(completion), result = WTF::move(result)]() mutable {
            completion(WTF::move(result));
        });
    });
}

void store(const String& url, CachedResponse&& response)
{
    auto& state = service();
    ASSERT(state.cache);
    size_t size = response.headers.size() + response.body.size();
    if (!yozora_byte_budget_reserve(&state.pending, size)) {
        std::fprintf(stderr, "yozora: http cache queue is full, a response of %zu bytes is not stored\n", size);
        return;
    }
    queue().dispatch([cache = state.cache, budget = &state.pending, size, url = url.isolatedCopy(), response = WTF::move(response)] {
        auto text = url.utf8();
        yozora_cache_entry entry { };
        std::snprintf(entry.url, sizeof(entry.url), "%s", text.data());
        entry.status = 200;
        entry.response_ms = response.response_ms;
        entry.headers = const_cast<char*>(response.headers.span().data());
        entry.headers_length = response.headers.size();
        entry.body = const_cast<uint8_t*>(response.body.span().data());
        entry.body_length = response.body.size();
        yozora_status status = yozora_httpcache_store(cache, &entry);
        if (status != YOZORA_OK && status != YOZORA_LIMIT)
            std::fprintf(stderr, "yozora: http cache store failed: %s\n", yozora_status_string(status));
        yozora_byte_budget_release(budget, size);
    });
}

bool allowsRequest(const ResourceRequest& request)
{
    return request.httpMethod() == "GET"_s && request.url().protocolIsInHTTPFamily() && !request.httpBody()
        && !request.hasHTTPHeaderField(HTTPHeaderName::Authorization) && !request.hasHTTPHeaderField(HTTPHeaderName::Range)
        && !request.hasHTTPHeaderField(HTTPHeaderName::IfMatch) && !request.hasHTTPHeaderField(HTTPHeaderName::IfRange);
}

bool isConditional(const ResourceRequest& request)
{
    return request.hasHTTPHeaderField(HTTPHeaderName::IfNoneMatch) || request.hasHTTPHeaderField(HTTPHeaderName::IfModifiedSince);
}

bool validatorsMatch(const ResourceRequest& request, const CachedResponse& cached)
{
    char value[256];
    auto stored = [&](const char* name) -> String {
        if (!yozora_cache_header(cached.headers.span().data(), cached.headers.size(), name, value, sizeof(value)))
            return { };
        return String::fromUTF8WithLatin1Fallback(std::span { value, strlen(value) });
    };
    // the entity tag decides when the request sends one, the date only when it is alone
    if (request.hasHTTPHeaderField(HTTPHeaderName::IfNoneMatch)) {
        auto etag = stored("ETag");
        return !etag.isEmpty() && request.httpHeaderField(HTTPHeaderName::IfNoneMatch).trim(deprecatedIsSpaceOrNewline) == etag;
    }
    auto modified = stored("Last-Modified");
    return !modified.isEmpty() && request.httpHeaderField(HTTPHeaderName::IfModifiedSince).trim(deprecatedIsSpaceOrNewline) == modified;
}

yozora_cache_mode mode(const ResourceRequest& request, bool history_load)
{
    // webcore marks its own validation of a held resource as a reload, during back and forward that is still a stored copy
    if (history_load && request.cachePolicy() != ResourceRequestCachePolicy::DoNotUseAnyCache)
        return YOZORA_CACHE_HISTORY;
    switch (request.cachePolicy()) {
    case ResourceRequestCachePolicy::ReloadIgnoringCacheData:
    case ResourceRequestCachePolicy::DoNotUseAnyCache:
    case ResourceRequestCachePolicy::RefreshAnyCacheData:
        return YOZORA_CACHE_RELOAD;
    case ResourceRequestCachePolicy::ReturnCacheDataElseLoad:
    case ResourceRequestCachePolicy::ReturnCacheDataDontLoad:
        return YOZORA_CACHE_HISTORY;
    case ResourceRequestCachePolicy::UseProtocolCachePolicy:
        break;
    }
    return history_load ? YOZORA_CACHE_HISTORY : YOZORA_CACHE_NORMAL;
}

yozora_cache_decision decide(const CachedResponse& response, yozora_cache_mode mode)
{
    yozora_cache_entry entry { };
    entry.status = 200;
    entry.response_ms = response.response_ms;
    entry.headers = const_cast<char*>(response.headers.span().data());
    entry.headers_length = response.headers.size();
    return yozora_cache_decide(&entry, mode, nowMilliseconds());
}

bool storableHeaders(const ResourceResponse& response, Vector<char>& block)
{
    if (response.httpStatusCode() != 200 || !serialize(responseHeaders(response), block))
        return false;
    yozora_cache_policy policy;
    yozora_cache_policy_parse(200, block.span().data(), block.size(), &policy);
    return policy.storable;
}

bool addValidators(ResourceRequest& request, const CachedResponse& response)
{
    char value[256];
    bool added = false;
    if (yozora_cache_header(response.headers.span().data(), response.headers.size(), "ETag", value, sizeof(value))) {
        request.setHTTPHeaderField(HTTPHeaderName::IfNoneMatch, String::fromUTF8WithLatin1Fallback(std::span { value, strlen(value) }));
        added = true;
    }
    if (yozora_cache_header(response.headers.span().data(), response.headers.size(), "Last-Modified", value, sizeof(value))) {
        request.setHTTPHeaderField(HTTPHeaderName::IfModifiedSince, String::fromUTF8WithLatin1Fallback(std::span { value, strlen(value) }));
        added = true;
    }
    return added;
}

void mergeRevalidation(CachedResponse& response, const ResourceResponse& not_modified)
{
    // the headers of a 304 replace the stored ones of the same name, the rest stays
    auto fresh = responseHeaders(not_modified);
    auto merged = fresh;
    for (auto& stored : parseBlock(response.headers)) {
        bool replaced = false;
        for (auto& header : fresh) {
            if (equalIgnoringASCIICase(header.first, stored.first))
                replaced = true;
        }
        if (!replaced)
            merged.append(stored);
    }
    Vector<char> block;
    if (serialize(merged, block))
        response.headers = WTF::move(block);
    response.response_ms = nowMilliseconds();
}

ResourceResponse makeResponse(const URL& url, const CachedResponse& cached, bool not_modified)
{
    ResourceResponse response;
    response.setURL(URL { url });
    response.setHTTPStatusCode(not_modified ? 304 : 200);
    response.setHTTPStatusText(not_modified ? "Not Modified"_s : "OK"_s);
    response.setHTTPVersion("HTTP/1.1"_s);
    for (auto& [name, value] : parseBlock(cached.headers))
        response.setHTTPHeaderField(name, value);
    auto length = not_modified ? 0 : cached.body.size();
    response.setHTTPHeaderField(HTTPHeaderName::ContentLength, String::number(length));
    response.setExpectedContentLength(static_cast<long long>(length));
    auto type = response.httpHeaderField(HTTPHeaderName::ContentType);
    response.setMimeType(extractMIMETypeFromMediaType(type).convertToASCIILowercase());
    response.setTextEncodingName(extractCharsetFromMediaType(type).toString());
    return response;
}

}
}
