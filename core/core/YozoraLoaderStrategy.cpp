#include "config.h"
#include <atomic>
#include "YozoraLoaderStrategy.h"
#include "BlobRegistry.h"
#include "BlobRegistryImpl.h"
#include "BlobDataFileReference.h"
#include "PolicyContainer.h"
#include "CachedResource.h"
#include "DocumentPage.h"
#include "YozoraCookies.h"
#include "YozoraHttpCache.h"
#include "YozoraAdblock.h"
#include "yozora_debug.h"
#include "YozoraCoreInternal.h"
#include "AudioDestination.h"
#include <curl/curl.h>
#include "CurlRequest.h"
#include "CurlRequestClient.h"
#include "CurlResponse.h"
#include "LocalFrame.h"
#include "LoaderStrategy.h"
#include "MediaStrategy.h"
#include "NetworkLoadMetrics.h"
#include "NetworkStateNotifier.h"
#include "Page.h"
#include "PlatformStrategies.h"
#include "ResourceError.h"
#include "ResourceLoader.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SharedBuffer.h"
#include "SubresourceLoader.h"
#include <cstdio>
#include <cstdlib>
#include <wtf/Deque.h>
#include <wtf/MonotonicTime.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/WallTime.h>
#include <wtf/RunLoop.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace WTF::StringLiterals;

static ResourceError load_error(const URL& url, int code, const String& message)
{
    return { "YozoraNetwork"_s, code, url, message };
}

// pages whose current load is a back or forward navigation, their requests may use any stored copy
static HashSet<const Page*>& desktop_pages()
{
    static auto* pages = new HashSet<const Page*>;
    return *pages;
}

// core thread only, like the loads that ask
static bool desktop_page(const Page* page) { return page && desktop_pages().contains(page); }

static HashSet<const Page*>& history_pages()
{
    static NeverDestroyed<HashSet<const Page*>> pages;
    return pages;
}

static int64_t now_milliseconds()
{
    return static_cast<int64_t>(WallTime::now().secondsSinceEpoch().milliseconds());
}

// the first challenge of a WWW-Authenticate header: its scheme and realm, empty scheme when it is one this browser does not answer
struct AuthChallenge {
    String scheme;
    String realm;
};

static AuthChallenge parse_challenge(const String& header)
{
    String trimmed = header.trim(isASCIIWhitespace<char16_t>);
    size_t space = trimmed.find(' ');
    String scheme = space == notFound ? trimmed : trimmed.left(space);
    if (!equalLettersIgnoringASCIICase(scheme, "basic"_s) && !equalLettersIgnoringASCIICase(scheme, "digest"_s))
        return { };
    String realm;
    size_t at = trimmed.findIgnoringASCIICase("realm="_s);
    if (at != notFound) {
        String rest = trimmed.substring(at + 6);
        if (rest.startsWith('"')) {
            size_t close = rest.find('"', 1);
            realm = close == notFound ? rest.substring(1) : rest.substring(1, close - 1);
        } else {
            size_t comma = rest.find(',');
            realm = comma == notFound ? rest : rest.left(comma);
        }
    }
    return { scheme.convertToASCIILowercase(), realm };
}

// what the user typed for a site, kept for the run so its other pages and scripts do not ask again. the key
// is origin and realm
static HashMap<String, std::pair<String, String>>& saved_credentials()
{
    static NeverDestroyed<HashMap<String, std::pair<String, String>>> saved;
    return saved;
}

void clearYozoraCredentials()
{
    saved_credentials().clear();
}

static String credential_key(const URL& url, const String& realm)
{
    return makeString(url.protocol(), "://"_s, url.host(), ':', url.port().value_or(url.protocolIs("https"_s) ? 443 : 80), '|', realm);
}

class YozoraNetworkJob;

// what a job needs from the strategy that queues it
class YozoraJobScheduler {
public:
    // a disk lookup that was counted in lookupStarted is over
    virtual void lookupFinished() = 0;
    // the job has nothing stored for it and waits for a connection slot
    virtual void needsSlot(YozoraNetworkJob&) = 0;
    // a response came over a connection that carries many requests at once, such as http/2
    virtual void hostMultiplexes(const String& host) = 0;
};

class YozoraNetworkJob final : public RefCounted<YozoraNetworkJob>, public CurlRequestClient {
public:
    static Ref<YozoraNetworkJob> create(ResourceLoader& loader, bool capture_metrics, YozoraJobScheduler& scheduler)
    {
        return adoptRef(*new YozoraNetworkJob(loader, capture_metrics, scheduler));
    }
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }
    // a job only talks to the network while the strategy has given it one of its connection slots
    bool holdsSlot() const { return m_holdsSlot; }
    void giveSlot(bool low)
    {
        m_holdsSlot = true;
        m_lowSlot = low;
        m_slotHost = hostKey();
    }
    // the origin whose connections the job uses: scheme, host and port, empty for what is no network address
    String hostKey() const
    {
        if (!m_loader || !m_loader->url().protocolIsInHTTPFamily())
            return { };
        return makeString(m_loader->url().protocol(), "://"_s, m_loader->url().host(), ':', m_loader->url().port().value_or(m_loader->url().protocolIs("https"_s) ? 443 : 80));
    }
    // the origin the job counted its slot against, a redirect to another one does not change it
    const String& slotHost() const { return m_slotHost; }
    // the job took its slot as a low priority one, such as a picture
    bool lowSlot() const { return m_lowSlot; }
    ResourceLoadPriority priority() const { return m_loader ? m_loader->request().priority() : ResourceLoadPriority::VeryLow; }
    // the stored copy is asked for before a connection slot is taken, so a slow disk does not hold a slot
    bool needsLookup()
    {
        return !m_cache_checked && !m_lookup && m_loader && cache_eligible();
    }
    void begin_lookup()
    {
        m_cache_checked = true;
        m_counted_lookup = true;
        if (!m_started)
            m_started = WTF::MonotonicTime::now();
        send_lookup();
    }
    void start()
    {
        if (!m_loader || !m_holdsSlot || m_request || m_deferred || m_lookup || m_replaying)
            return;
        if (!m_started)
            m_started = WTF::MonotonicTime::now();
        if (!m_cache_checked) {
            m_cache_checked = true;
            if (check_cache())
                return;
        }
        start_network();
    }
    void detach()
    {
        m_loader = nullptr;
        m_stale.reset();
        m_store.reset();
        if (auto request = std::exchange(m_request, nullptr)) {
            request->invalidateClient();
            request->cancel();
        }
    }
    void defer(bool value)
    {
        m_deferred = value;
        if (!value && m_responseCompletion) {
            auto completion = WTF::move(m_responseCompletion);
            completion();
        }
        if (!value)
            start();
    }
private:
    struct StoreState {
        Vector<char> headers;
        Vector<uint8_t> body;
    };

    YozoraNetworkJob(ResourceLoader& loader, bool capture_metrics, YozoraJobScheduler& scheduler)
        : m_loader(loader)
        , m_scheduler(scheduler)
        , m_deferred(loader.defersLoading())
        , m_capture_metrics(capture_metrics)
    { }
    // sets the cache mode of the request, true when a stored copy may be used for it
    bool cache_eligible()
    {
        if (!YozoraHttpCache::enabled() || !YozoraHttpCache::allowsRequest(m_loader->request()))
            return false;
        RefPtr page = m_loader->frame() ? m_loader->frame()->page() : nullptr;
        if (desktop_page(page.get()))
            return false;
        bool history = page && history_pages().contains(page.get());
        m_cache_mode = YozoraHttpCache::mode(m_loader->request(), history);
        // the offline policy is a request the page makes itself, and it fails when nothing is stored
        m_cache_only = m_loader->request().cachePolicy() == ResourceRequestCachePolicy::ReturnCacheDataDontLoad;
        return m_cache_mode != YOZORA_CACHE_RELOAD;
    }
    void send_lookup()
    {
        m_lookup = true;
        YozoraHttpCache::lookup(m_loader->url().stringWithoutFragmentIdentifier(),
            [job = Ref { *this }](std::optional<CachedResponse>&& entry) mutable { job->cache_looked_up(WTF::move(entry)); });
    }
    // a redirect looks the next address up while the job keeps its slot, true when it waits for the disk
    bool check_cache()
    {
        if (!cache_eligible())
            return false;
        send_lookup();
        return true;
    }
    void cache_looked_up(std::optional<CachedResponse>&& entry)
    {
        Ref protectedThis { *this };
        m_lookup = false;
        if (std::exchange(m_counted_lookup, false))
            m_scheduler.lookupFinished();
        RefPtr loader = m_loader;
        if (!loader || loader->reachedTerminalState())
            return;
        auto decision = entry ? YozoraHttpCache::decide(*entry, m_cache_mode) : YOZORA_CACHE_USE_NETWORK;
        if (m_cache_only && !entry)
            return fail(load_error(loader->url(), 20, "Page is not stored for offline use"_s));
        bool conditional = YozoraHttpCache::isConditional(loader->request());
        if (entry && decision == YOZORA_CACHE_USE_STORED) {
            // a page that asks about the copy it holds gets a 304 when it is the stored one
            bool not_modified = conditional && YozoraHttpCache::validatorsMatch(loader->request(), *entry);
            log_cache(not_modified ? "not modified" : "hit", loader->url());
            return replay(WTF::move(*entry), not_modified);
        }
        // the page's own conditional request already carries validators, it goes to the server as it is
        if (entry && decision == YOZORA_CACHE_USE_VALIDATE && !conditional)
            m_stale = WTF::move(entry);
        if (m_holdsSlot)
            start();
        else
            m_scheduler.needsSlot(*this);
    }
    void start_network()
    {
        ResourceRequest request = m_loader->request();
        // a copy that is stale is asked about before it is used again, the server answers 304 and sends no body
        if (m_stale && !YozoraHttpCache::addValidators(request, *m_stale))
            m_stale.reset();
        // the loader hands requests over without a Cookie header: the store owns the cookies of every hop, redirects included
        if (auto cookies = YozoraCookies::requestHeader(request); !cookies.isEmpty())
            request.setHTTPHeaderField(HTTPHeaderName::Cookie, cookies);
        else
            request.removeHTTPHeaderField(HTTPHeaderName::Cookie);
        m_request = CurlRequest::create(request, *this, m_capture_metrics ? CurlRequest::CaptureNetworkLoadMetrics::Extended : CurlRequest::CaptureNetworkLoadMetrics::Basic);
        if (!m_user.isEmpty() || !m_password.isEmpty())
            m_request->setUserPass(m_user, m_password);
        m_request->resume();
    }
    void log_cache(const char* what, const URL& url)
    {
        if (std::getenv("YOZORA_LOG_REQUESTS")) {
            auto text = url.string().left(90).utf8();
            std::fprintf(stderr, "net: cache %s  %s\n", what, text.data());
        }
    }
    // the stored body goes to the loader the way a network body would
    void replay(CachedResponse&& cached, bool not_modified)
    {
        Ref protectedThis { *this };
        RefPtr loader = m_loader;
        // servePendingRequests calls start() on every job, this one already has its answer
        m_replaying = true;
        auto response = YozoraHttpCache::makeResponse(loader->url(), cached, not_modified);
        Vector<uint8_t> body;
        if (!not_modified)
            body = WTF::move(cached.body);
        loader->didReceiveResponse(WTF::move(response), [this, protectedThis, body = WTF::move(body)]() mutable {
            auto resume = [this, protectedThis, body = WTF::move(body)]() mutable {
                RefPtr loader = m_loader;
                if (!loader || loader->reachedTerminalState())
                    return;
                m_received = body.size();
                if (!body.isEmpty())
                    loader->didReceiveData(SharedBuffer::create(WTF::move(body)), m_received, DataPayloadBytes);
                if (!loader->reachedTerminalState())
                    loader->didFinishLoading(NetworkLoadMetrics::emptyMetrics());
                detach();
            };
            if (m_deferred)
                m_responseCompletion = WTF::move(resume);
            else
                resume();
        });
    }
    void fail(const ResourceError& error)
    {
        Ref protectedThis { *this };
        RefPtr loader = m_loader;
        if (std::getenv("YOZORA_LOG_REQUESTS") && !error.isCancellation()) {
            auto url = error.failingURL().string().utf8();
            auto description = error.localizedDescription().utf8();
            std::fprintf(stderr, "net: failed   %s (%d) %s\n", url.data(), error.errorCode(), description.data());
        }
        if (loader && !loader->reachedTerminalState())
            loader->didFail(error);
        detach();
    }
    void curlDidSendData(CurlRequest&, unsigned long long sent, unsigned long long total) final
    {
        // curl reports the bytes sent so far, the debug overlay adds only what is new
        if (sent > m_sentCounted) {
            yozora_traffic_add(0, sent - m_sentCounted);
            m_sentCounted = sent;
        }
        if (RefPtr loader = m_loader)
            loader->didSendData(sent, total);
    }
    void curlDidReceiveResponse(CurlRequest& request, CurlResponse&& response) final
    {
        Ref protectedThis { *this };
        RefPtr loader = m_loader;
        if (!loader || m_request.get() != &request)
            return;
        YozoraCookies::receive(loader->request(), response);
        if (response.statusCode == 401 && !m_auth_declined && ask_for_credentials(request, response))
            return;
        handle_response(request, WTF::move(response));
    }

    // a 401 with a scheme this browser answers: earlier credentials are sent or the user is asked. true when
    // the load waits
    bool ask_for_credentials(CurlRequest& request, const CurlResponse& response)
    {
        RefPtr loader = m_loader;
        String header;
        for (auto& line : response.headers) {
            if (line.startsWithIgnoringASCIICase("www-authenticate:"_s)) {
                header = line.substring(17);
                break;
            }
        }
        auto challenge = parse_challenge(header);
        if (challenge.scheme.isEmpty())
            return false;
        auto key = credential_key(loader->url(), challenge.realm);
        auto saved = saved_credentials().find(key);
        // credentials that were sent and refused are dropped, the second 401 asks the user
        if (saved != saved_credentials().end() && !m_credentials_refused && !m_credentials_sent) {
            retry_with(request, saved->value.first, saved->value.second);
            return true;
        }
        if (m_credentials_sent) {
            saved_credentials().remove(key);
            m_credentials_refused = true;
        }
        // only the page itself asks: a picture or a script behind a login gets its 401
        RefPtr frame = loader->frame();
        yozora_core* core = frame && loader->request().requester() == ResourceRequestRequester::Main ? yozora_core_for_page(frame->page()) : nullptr;
        if (!core)
            return false;
        // the answer of this response waits, the transfer is paused until the response is handed over or the request replaced
        auto stashed = std::make_shared<CurlResponse>(response);
        auto url = loader->url();
        yozora_core_ask_credentials(*core, url.host().toString(), url.port().value_or(url.protocolIs("https"_s) ? 443 : 80), challenge.realm, challenge.scheme,
            url.protocolIs("https"_s), m_credentials_refused ? 1 : 0,
            [this, protectedThis = Ref { *this }, request = Ref { request }, stashed, key](std::optional<std::pair<String, String>>&& credentials) {
                if (!m_loader || m_loader->reachedTerminalState() || m_request.get() != request.ptr())
                    return;
                if (!credentials) {
                    // the user refused: the page shows what the server said
                    m_auth_declined = true;
                    handle_response(*m_request, CurlResponse(*stashed));
                    return;
                }
                saved_credentials().set(key, *credentials);
                retry_with(*m_request, credentials->first, credentials->second);
            });
        return true;
    }

    void retry_with(CurlRequest& request, const String& user, const String& password)
    {
        request.invalidateClient();
        request.cancel();
        m_request = nullptr;
        m_user = user;
        m_password = password;
        m_credentials_sent = true;
        start();
    }

    void handle_response(CurlRequest& request, CurlResponse&& response)
    {
        Ref protectedThis { *this };
        RefPtr loader = m_loader;
        if (!loader)
            return;
        // http/2 and newer carry the requests to a host over one connection, so curl does not queue them
        if (response.httpVersion >= CURL_HTTP_VERSION_2_0)
            m_scheduler.hostMultiplexes(hostKey());
        ResourceResponse parsed(response);
        if (parsed.isRedirection() && parsed.httpStatusCode() != 304) {
            if (++m_redirects > 20)
                return fail(load_error(loader->url(), 1, "Too many redirects"_s));
            ResourceRequest redirected(loader->request());
            URL next(loader->url(), parsed.httpHeaderField(HTTPHeaderName::Location));
            if (!next.isValid() || !next.protocolIsInHTTPFamily() || !next.user().isEmpty() || !next.password().isEmpty())
                return fail(load_error(next, 2, "Redirect URL is not allowed"_s));
            if (!protocolHostAndPortAreEqual(loader->url(), next)) {
                m_user = String();
                m_password = String();
                m_credentials_sent = false;
                m_credentials_refused = false;
                redirected.removeHTTPHeaderField(HTTPHeaderName::Authorization);
                redirected.removeHTTPHeaderField(HTTPHeaderName::Cookie);
                redirected.removeHTTPHeaderField(HTTPHeaderName::Origin);
            }
            if (loader->url().protocolIs("https"_s) && next.protocolIs("http"_s))
                redirected.clearHTTPReferrer();
            if (parsed.httpStatusCode() == 303 || ((parsed.httpStatusCode() == 301 || parsed.httpStatusCode() == 302) && redirected.httpMethod() == "POST"_s)) {
                redirected.setHTTPMethod("GET"_s);
                redirected.setHTTPBody(nullptr);
                redirected.removeHTTPHeaderField(HTTPHeaderName::ContentType);
                redirected.removeHTTPHeaderField(HTTPHeaderName::ContentLength);
            }
            redirected.setURL(WTF::move(next));
            request.invalidateClient();
            request.cancel();
            m_request = nullptr;
            // the next address has its own stored copy and its own validators
            m_stale.reset();
            m_store.reset();
            m_cache_checked = false;
            loader->willSendRequest(WTF::move(redirected), parsed, [this, protectedThis](ResourceRequest&& approved) {
                if (!m_loader || m_loader->reachedTerminalState())
                    return;
                if (approved.isNull())
                    return fail(load_error(m_loader->url(), 3, "Redirect rejected by page policy"_s));
                m_loader->setRequest(WTF::move(approved));
                start();
            });
            return;
        }
        if (m_stale && parsed.httpStatusCode() == 304) {
            request.invalidateClient();
            request.cancel();
            m_request = nullptr;
            auto cached = WTF::move(*m_stale);
            m_stale.reset();
            YozoraHttpCache::mergeRevalidation(cached, parsed);
            log_cache("validated", loader->url());
            CachedResponse copy = cached;
            YozoraHttpCache::store(loader->url().stringWithoutFragmentIdentifier(), WTF::move(copy));
            return replay(WTF::move(cached), false);
        }
        // a full answer replaces whatever was stored
        m_stale.reset();
        m_store.reset();
        RefPtr storingPage = loader->frame() ? loader->frame()->page() : nullptr;
        if (YozoraHttpCache::enabled() && !desktop_page(storingPage.get()) && YozoraHttpCache::allowsRequest(loader->request()) && parsed.expectedContentLength() <= YOZORA_HTTPCACHE_BODY_CAP) {
            StoreState state;
            if (YozoraHttpCache::storableHeaders(parsed, state.headers))
                m_store = WTF::move(state);
        }
        if (parsed.httpStatusCode() == 407)
            return fail(load_error(loader->url(), 5, "Proxy authentication is not available"_s));
        loader->didReceiveResponse(WTF::move(parsed), [this, protectedThis, request = Ref { request }] {
            auto resume = [request] { request->completeDidReceiveResponse(); };
            if (m_deferred)
                m_responseCompletion = WTF::move(resume);
            else if (m_loader && !m_loader->reachedTerminalState())
                resume();
        });
    }
    void curlDidReceiveData(CurlRequest& request, Ref<SharedBuffer>&& data) final
    {
        Ref protectedThis { *this };
        RefPtr loader = m_loader;
        if (!loader || m_request.get() != &request || loader->reachedTerminalState())
            return;
        m_received += data->size();
        yozora_traffic_add(data->size(), 0);
        if (m_store) {
            // the body is decoded here, so the cache limit applies to what is kept, not to what was announced
            if (m_store->body.size() + data->size() > YOZORA_HTTPCACHE_BODY_CAP)
                m_store.reset();
            else
                m_store->body.append(data->span());
        }
        loader->didReceiveData(data, data->size(), DataPayloadBytes);
    }
    void curlDidComplete(CurlRequest& request, NetworkLoadMetrics&& metrics) final
    {
        Ref protectedThis { *this };
        RefPtr loader = m_loader;
        if (loader && std::getenv("YOZORA_LOG_REQUESTS")) {
            auto time = [&](WTF::MonotonicTime value) { return value ? (value - m_started).milliseconds() : -1.0; };
            auto text = loader->url().string().left(90).utf8();
            std::fprintf(stderr, "net: %6.0f ms total, dns %.0f, connect %.0f, tls %.0f, first byte %.0f, %zu bytes  %s\n",
                time(WTF::MonotonicTime::now()), time(metrics.domainLookupEnd), time(metrics.connectEnd), time(metrics.secureConnectionStart),
                time(metrics.responseStart), m_received, text.data());
        }
        if (loader && m_request.get() == &request && !loader->reachedTerminalState()) {
            if (m_store) {
                CachedResponse entry;
                entry.response_ms = now_milliseconds();
                entry.headers = WTF::move(m_store->headers);
                entry.body = WTF::move(m_store->body);
                m_store.reset();
                log_cache("stored", loader->url());
                YozoraHttpCache::store(loader->url().stringWithoutFragmentIdentifier(), WTF::move(entry));
            }
            loader->didFinishLoading(metrics);
        }
        detach();
    }
    void curlDidFailWithError(CurlRequest& request, ResourceError&& error, CertificateInfo&&) final
    {
        if (m_request.get() == &request)
            fail(error);
    }
    RefPtr<ResourceLoader> m_loader;
    YozoraJobScheduler& m_scheduler;
    RefPtr<CurlRequest> m_request;
    Function<void()> m_responseCompletion;
    // a stored copy waiting for the server to confirm it, and the body being kept for the next visit
    std::optional<CachedResponse> m_stale;
    std::optional<StoreState> m_store;
    size_t m_received { 0 };
    unsigned long long m_sentCounted { 0 };
    unsigned m_redirects { 0 };
    // what the user typed for this load, sent with the request again; refused means the server said no to it
    String m_user;
    String m_password;
    bool m_credentials_sent { false };
    bool m_credentials_refused { false };
    bool m_auth_declined { false };
    yozora_cache_mode m_cache_mode { YOZORA_CACHE_NORMAL };
    bool m_cache_only { false };
    bool m_cache_checked { false };
    bool m_lookup { false };
    bool m_counted_lookup { false };
    bool m_replaying { false };
    bool m_deferred { false };
    bool m_holdsSlot { false };
    bool m_lowSlot { false };
    String m_slotHost;
    bool m_capture_metrics { false };
    WTF::MonotonicTime m_started;
};

class YozoraLoaderStrategy final : public LoaderStrategy, public YozoraJobScheduler {
public:
    void loadResource(LocalFrame& frame, CachedResource& resource, ResourceRequest&& request, const ResourceLoaderOptions& options, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion) final
    {
        // a page the user opens by address always loads, only what it pulls in can be blocked. webcore loads
        // an iframe document as a main resource too, so ad frames need the frame check
        bool is_document = resource.type() == CachedResource::Type::MainResource && frame.isMainFrame();
        auto filter_type = YozoraAdblock::typeOf(resource.type(), frame.isMainFrame());
        SubresourceLoader::create(frame, resource, WTF::move(request), options,
            [this, is_document, filter_type, completion = WTF::move(completion)](RefPtr<SubresourceLoader>&& loader) mutable {
                RefPtr retained = loader;
                completion(WTF::move(loader));
                if (!retained || retained->reachedTerminalState())
                    return;
                // challenge workers use blob scripts, WebCore owns their origin checks and resource handles
                if (retained->url().protocolIsData() || retained->url().protocolIsBlob()) {
                    retained->start();
                    return;
                }
                if (!retained->url().protocolIsInHTTPFamily() || !ResourceLoader::isPortAllowed(retained->url())) {
                    retained->didFail(cannotShowURLError(retained->request()));
                    return;
                }
                if (!is_document && YozoraAdblock::blocks(retained->frame() ? retained->frame()->page() : nullptr, filter_type, retained->url(), retained->request().firstPartyForCookies())) {
                    retained->didFail(blockedByContentBlockerError(retained->request()));
                    return;
                }
                if (m_jobs.size() >= maxQueuedRequests) {
                    retained->didFail(load_error(retained->url(), 6, "Too many resource requests"_s));
                    return;
                }
                Ref job = YozoraNetworkJob::create(*retained, m_captureMetrics, *this);
                m_jobs.add(retained.get(), job.copyRef());
                m_pending.append(WTF::move(job));
                startPending();
            });
    }
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& data) final
    {
        response = { };
        data.clear();
        error = load_error(request.url(), 7, "Synchronous network requests are not available"_s);
    }
    void pageLoadCompleted(Page&) final { servePendingRequests(); }
    void browsingContextRemoved(LocalFrame& frame) final
    {
        Vector<RefPtr<ResourceLoader>> loaders;
        for (auto* loader : m_jobs.keys()) {
            if (loader->frame() == &frame)
                loaders.append(loader);
        }
        for (auto& loader : loaders)
            loader->cancel();
    }
    void remove(ResourceLoader* loader) final
    {
        auto job = m_jobs.take(loader);
        if (!job)
            return;
        job->detach();
        if (job->holdsSlot()) {
            --m_active;
            if (job->lowSlot())
                --m_activeLow;
            releaseHostSlot(job->slotHost());
            startPending();
        } else
            m_pending.removeFirstMatching([&](auto& queued) { return queued.ptr() == job.get(); });
    }
    void setDefersLoading(ResourceLoader& loader, bool deferred) final
    {
        if (auto job = m_jobs.get(&loader))
            job->defer(deferred);
    }
    void crossOriginRedirectReceived(ResourceLoader* loader, const URL&) final
    {
        // WebCore already checks redirect CORS policy before this bookkeeping hook
        RELEASE_ASSERT(loader && m_jobs.contains(loader));
    }
    void servePendingRequests(ResourceLoadPriority = ResourceLoadPriority::VeryLow) final
    {
        Vector<Ref<YozoraNetworkJob>> jobs;
        for (auto& job : m_jobs.values())
            jobs.append(*job);
        for (auto& job : jobs)
            job->start();
        startPending();
    }
    void suspendPendingRequests() final
    {
        for (auto& job : m_jobs.values())
            job->defer(true);
    }
    void resumePendingRequests() final
    {
        Vector<Ref<YozoraNetworkJob>> jobs;
        for (auto& job : m_jobs.values())
            jobs.append(*job);
        for (auto& job : jobs)
            job->defer(false);
    }
    bool usePingLoad() const final { return false; }
    void startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion) final
    {
        if (completion)
            completion(load_error(request.url(), 8, "Ping requests are disabled"_s), { });
    }
    void preconnectTo(FrameLoader&, ResourceRequest&& request, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion) final
    {
        completion(load_error(request.url(), 9, "Preconnect is not available"_s));
    }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool enabled) final { m_captureMetrics = enabled; }
    bool isOnLine() const final { return NetworkStateNotifier::singleton().onLine(); }
    void addOnlineStateChangeListener(Function<void(bool)>&& listener) final { NetworkStateNotifier::singleton().addListener(WTF::move(listener)); }
    void isResourceLoadFinished(CachedResource& resource, CompletionHandler<void(bool)>&& completion) final { completion(!resource.isLoading()); }
    ResourceError cancelledError(const ResourceRequest& request) const final
    {
        auto error = load_error(request.url(), 10, "Request cancelled"_s);
        error.setType(ResourceError::Type::Cancellation);
        return error;
    }
    ResourceError blockedError(const ResourceRequest& request) const final { return load_error(request.url(), 11, "Request blocked"_s); }
    bool isBlockedError(const ResourceError& error) const final { return error.domain() == "YozoraNetwork"_s && error.errorCode() == 11; }
    ResourceError blockedByContentBlockerError(const ResourceRequest& request) const final { return load_error(request.url(), 12, "Blocked by content policy"_s); }
    ResourceError cannotShowURLError(const ResourceRequest& request) const final { return load_error(request.url(), 13, "Unsupported URL"_s); }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& request) const final { return load_error(request.url(), 14, "Navigation policy changed"_s); }
    ResourceError cannotShowMIMETypeError(const ResourceResponse& response) const final { return load_error(response.url(), 15, "Unsupported response type"_s); }
    ResourceError fileDoesNotExistError(const ResourceResponse& response) const final { return load_error(response.url(), 16, "File does not exist"_s); }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& request) const final { return load_error(request.url(), 17, "HTTPS redirect loop"_s); }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const final { return load_error(request.url(), 18, "HTTPS is required"_s); }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError& error) const final { return error.domain() == "YozoraNetwork"_s && error.errorCode() == 18; }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& response) const final { return load_error(response.url(), 19, "Plugins are unavailable"_s); }
private:
    void lookupFinished() final
    {
        --m_lookups;
        startPending();
    }
    void needsSlot(YozoraNetworkJob& job) final
    {
        m_pending.append(Ref { job });
        startPending();
    }
    void hostMultiplexes(const String& host) final
    {
        if (host.isEmpty() || !m_multiplexedHosts.add(host).isNewEntry)
            return;
        // the requests that waited for a connection of this host can all go now
        startPending();
    }
    // curl serves waiting requests of a host in arrival order and lets a later one take a connection that
    // just freed, so a stylesheet queued behind pictures could wait for all of them. up to six requests to a
    // host not known to carry many go to curl, the rest wait here, the most important first
    bool hostHasRoom(const String& host) const
    {
        if (host.isEmpty() || m_multiplexedHosts.contains(host))
            return true;
        auto found = m_activeByHost.find(host);
        return found == m_activeByHost.end() || found->value < maxRequestsPerHost;
    }
    void releaseHostSlot(const String& host)
    {
        if (host.isEmpty())
            return;
        auto found = m_activeByHost.find(host);
        if (found == m_activeByHost.end())
            return;
        if (!--found->value)
            m_activeByHost.remove(found);
    }
    // requests over the limits wait here, the most important first and equal ones in WebCore's order: a
    // stylesheet must not wait behind pictures asked for earlier
    void startPending()
    {
        while (!m_pending.isEmpty()) {
            std::optional<size_t> best;
            bool best_lookup = false;
            for (size_t i = 0; i < m_pending.size(); ++i) {
                auto& job = m_pending[i];
                bool lookup = job->needsLookup();
                if (lookup ? m_lookups >= maxLookups : m_active >= maxActiveRequests)
                    continue;
                if (!lookup && !hostHasRoom(job->hostKey()))
                    continue;
                // pictures never take the last slots: a stylesheet or a script found later starts at once
                if (!lookup && isLow(job->priority()) && m_activeLow >= maxActiveRequests - reservedSlots)
                    continue;
                if (!best || job->priority() > m_pending[*best]->priority()) {
                    best = i;
                    best_lookup = lookup;
                }
            }
            if (!best)
                return;
            Ref job = m_pending[*best];
            m_pending.removeAt(*best);
            if (best_lookup) {
                ++m_lookups;
                job->begin_lookup();
            } else {
                bool low = isLow(job->priority());
                job->giveSlot(low);
                if (!job->slotHost().isEmpty())
                    ++m_activeByHost.add(job->slotHost(), 0).iterator->value;
                ++m_active;
                if (low)
                    ++m_activeLow;
                job->start();
            }
        }
    }

    static bool isLow(ResourceLoadPriority priority) { return priority <= ResourceLoadPriority::Low; }

    // a page of images asks for dozens of resources at once, the phone serves a few at a time
    static constexpr size_t maxActiveRequests = 12;
    static constexpr size_t reservedSlots = 3;
    // the connections curl opens to one host
    static constexpr size_t maxRequestsPerHost = 6;
    // stored bodies wait in memory until their job takes them, a megabyte each at most
    static constexpr size_t maxLookups = 6;
    // waiting requests are bounded too, a page that asks for more fails the surplus
    static constexpr size_t maxQueuedRequests = 1024;
    HashMap<ResourceLoader*, RefPtr<YozoraNetworkJob>> m_jobs;
    Vector<Ref<YozoraNetworkJob>> m_pending;
    HashMap<String, size_t> m_activeByHost;
    HashSet<String> m_multiplexedHosts;
    size_t m_active { 0 };
    size_t m_activeLow { 0 };
    size_t m_lookups { 0 };
    bool m_captureMetrics { false };
};

class YozoraBlobRegistry final : public BlobRegistry {
public:
    void registerInternalFileBlobURL(const URL& url, Ref<BlobDataFileReference>&& file, const String&, const String& type) final { m_registry.registerInternalFileBlobURL(url, WTF::move(file), type); }
    void registerInternalBlobURL(const URL& url, Vector<BlobPart>&& parts, const String& type) final { m_registry.registerInternalBlobURL(url, WTF::move(parts), type); }
    void registerBlobURL(const URL& url, const URL& source, const PolicyContainer& policy, const std::optional<SecurityOriginData>& origin) final { m_registry.registerBlobURL(url, source, policy, origin); }
    void registerInternalBlobURLOptionallyFileBacked(const URL& url, const URL& source, RefPtr<BlobDataFileReference>&& file, const String& type) final { m_registry.registerInternalBlobURLOptionallyFileBacked(url, source, WTF::move(file), type, { }); }
    void registerInternalBlobURLForSlice(const URL& url, const URL& source, long long start, long long end, const String& type) final { m_registry.registerInternalBlobURLForSlice(url, source, start, end, type); }
    void unregisterBlobURL(const URL& url, const std::optional<SecurityOriginData>& origin) final { m_registry.unregisterBlobURL(url, origin); }
    void registerBlobURLHandle(const URL& url, const std::optional<SecurityOriginData>& origin) final { m_registry.registerBlobURLHandle(url, origin); }
    void unregisterBlobURLHandle(const URL& url, const std::optional<SecurityOriginData>& origin) final { m_registry.unregisterBlobURLHandle(url, origin); }
    String blobType(const URL& url) final { return m_registry.blobType(url); }
    unsigned long long blobSize(const URL& url) final { return m_registry.blobSize(url); }
    void writeBlobsToTemporaryFilesForIndexedDB(const Vector<String>& urls, CompletionHandler<void(Vector<String>&&)>&& completion) final { m_registry.writeBlobsToTemporaryFilesForIndexedDB(urls, WTF::move(completion)); }
    BlobRegistryImpl* blobRegistryImpl() final { return &m_registry; }
private:
    BlobRegistryImpl m_registry;
};

class YozoraMediaStrategy final : public MediaStrategy {
#if ENABLE(WEB_AUDIO)
    // the destination of the port feeds the sound device itself, there is no other process to ask
    Ref<AudioDestination> createAudioDestination(const AudioDestinationCreationOptions& options) final { return AudioDestination::create(options); }
#endif
};
class YozoraPlatformStrategies final : public PlatformStrategies {
    LoaderStrategy* createLoaderStrategy() final { return new YozoraLoaderStrategy; }
    // yozora's pasteboard owns its bounded in-process data without a strategy
    PasteboardStrategy* createPasteboardStrategy() final { RELEASE_ASSERT_NOT_REACHED(); }
    MediaStrategy* createMediaStrategy() final { return new YozoraMediaStrategy; }
    BlobRegistry* createBlobRegistry() final { return new YozoraBlobRegistry; }
};

void installYozoraPlatformStrategies()
{
    RELEASE_ASSERT(!hasPlatformStrategies());
    setPlatformStrategies(new YozoraPlatformStrategies);
}

void setYozoraHistoryLoad(const Page& page, bool active)
{
    if (active)
        history_pages().add(&page);
    else
        history_pages().remove(&page);
}

void setYozoraDesktopPage(const Page& page, bool active)
{
    if (active)
        desktop_pages().add(&page);
    else
        desktop_pages().remove(&page);
}

}
