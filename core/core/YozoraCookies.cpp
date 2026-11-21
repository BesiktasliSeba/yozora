#include "config.h"
#include "YozoraCookies.h"
#include "yozora_core.h"
#include "YozoraCoreThread.h"
#include "YozoraPrivateSession.h"
#include "CookieJar.h"
#include "CookieJarDB.h"
#include "CurlResponse.h"
#include "NetworkStorageSession.h"
#include "ResourceRequest.h"
#include "SameSiteInfo.h"
#include <pal/SessionID.h>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <wtf/text/StringToIntegerConversion.h>

using namespace WebCore;

namespace {

pthread_mutex_t path_lock = PTHREAD_MUTEX_INITIALIZER;
// core thread only, read when the store is created and set by the settings
bool third_party_blocked { false };
bool session_created { false };
bool private_created { false };
std::string& store_path()
{
    static std::string* path = new std::string;
    return *path;
}

}

namespace YozoraCookies {

NetworkStorageSession& session()
{
    static NetworkStorageSession* storage = [] {
        // an ephemeral id only picks the in memory database for the constructor, the file replaces it below
        auto* result = new NetworkStorageSession(PAL::SessionID::generateEphemeralSessionID());
        pthread_mutex_lock(&path_lock);
        std::string path = store_path();
        pthread_mutex_unlock(&path_lock);
        if (!path.empty())
            result->setCookieDatabase(makeUniqueRef<CookieJarDB>(String::fromUTF8(path.c_str())));
        result->setCookieAcceptPolicy(third_party_blocked ? CookieAcceptPolicy::OnlyFromMainDocumentDomain : CookieAcceptPolicy::Always);
        session_created = true;
        return result;
    }();
    return *storage;
}

// private session cookies: an ephemeral id selects the in memory database and no file replaces it, so nothing
// reaches the profile
static NetworkStorageSession& privateSession()
{
    static NetworkStorageSession* storage = [] {
        auto* result = new NetworkStorageSession(PAL::SessionID::generateEphemeralSessionID());
        result->setCookieAcceptPolicy(third_party_blocked ? CookieAcceptPolicy::OnlyFromMainDocumentDomain : CookieAcceptPolicy::Always);
        private_created = true;
        return result;
    }();
    return *storage;
}

NetworkStorageSession& current()
{
    return YozoraPrivateSession::active() ? privateSession() : session();
}

String requestHeader(const ResourceRequest& request)
{
    if (!request.allowCookies())
        return { };
    auto [value, secure] = current().cookieRequestHeaderFieldValue(request.firstPartyForCookies(), SameSiteInfo::create(request), request.url(), std::nullopt, std::nullopt,
        CookieJar::shouldIncludeSecureCookies(request.url()), ApplyTrackingPrevention::No, ShouldRelaxThirdPartyCookieBlocking::No, IsKnownCrossSiteTracker::No);
    return value;
}

String headerForURL(const URL& url)
{
    if (!url.isValid())
        return { };
    ResourceRequest request { URL { url } };
    request.setFirstPartyForCookies(url);
    return requestHeader(request);
}

void receive(const ResourceRequest& request, const CurlResponse& response)
{
    if (!request.allowCookies())
        return;
    constexpr auto prefix = "set-cookie:"_s;
    for (auto& line : response.headers) {
        if (line.length() > prefix.length() && line.startsWithIgnoringASCIICase(prefix))
            current().setCookiesFromHTTPResponse(request.firstPartyForCookies(), request.url(), line.substring(prefix.length()).trim(isASCIIWhitespace<char16_t>));
    }
}

void setStorePath(const std::string& path)
{
    pthread_mutex_lock(&path_lock);
    store_path() = path;
    pthread_mutex_unlock(&path_lock);
}

void clear()
{
    session().deleteAllCookies([] { });
}

void clearPrivate()
{
    if (private_created)
        privateSession().deleteAllCookies([] { });
}

void setThirdPartyBlocked(bool blocked)
{
    third_party_blocked = blocked;
    // the database opens through WTF's main thread, which does not exist before the first core: the store reads the flag when it is made
    auto policy = blocked ? CookieAcceptPolicy::OnlyFromMainDocumentDomain : CookieAcceptPolicy::Always;
    if (session_created)
        session().setCookieAcceptPolicy(policy);
    if (private_created)
        privateSession().setCookieAcceptPolicy(policy);
}

}
