#pragma once

#include "yozora_httpcache.h"
#include <optional>
#include <wtf/Function.h>
#include <wtf/URL.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class ResourceRequest;
class ResourceResponse;


// the stored form of a response: the decoded body and the header lines worth replaying
struct CachedResponse {
    int64_t response_ms { 0 };
    Vector<char> headers;
    Vector<uint8_t> body;
};

namespace YozoraHttpCache {

// opens the directory and starts the thread that owns it, once per process
bool open(const char* directory, uint64_t max_bytes, char* error, size_t capacity);
bool enabled();
void clear();
// returns after every queued write reached the disk
void flush();
// the completion runs on the main thread, with nothing when no usable copy is stored
void lookup(const String& url, Function<void(std::optional<CachedResponse>&&)>&& completion);
void store(const String& url, CachedResponse&&);

// only plain GET requests use the cache, a ranged or authenticated one never does
bool allowsRequest(const ResourceRequest&);
// the page already holds a copy and asks whether it is still current
bool isConditional(const ResourceRequest&);
// the conditional request names the validator of the stored copy
bool validatorsMatch(const ResourceRequest&, const CachedResponse&);
yozora_cache_mode mode(const ResourceRequest&, bool history_load);
yozora_cache_decision decide(const CachedResponse&, yozora_cache_mode);
// false when the response is too large or unsafe to keep
bool storableHeaders(const ResourceResponse&, Vector<char>& block);
bool addValidators(ResourceRequest&, const CachedResponse&);
void mergeRevalidation(CachedResponse&, const ResourceResponse& not_modified);
// not_modified makes the answer a 304 without a body, for a page that holds the same copy
ResourceResponse makeResponse(const URL&, const CachedResponse&, bool not_modified);

}
}
