#pragma once

#include <string>
#include <wtf/URL.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class ResourceRequest;
class CurlResponse;
class NetworkStorageSession;
}

namespace YozoraCookies {

// the cookie store of the profile, shared by every tab, created on first use on the core thread
WebCore::NetworkStorageSession& session();
// the store a request uses: the profile, or while the private session runs a store in memory that clearPrivate empties
WebCore::NetworkStorageSession& current();
// the Cookie header value for a request, empty when the page forbids cookies or none match
String requestHeader(const WebCore::ResourceRequest&);
// the Cookie header the store of the moment would send to url as its own first party: HttpOnly and Secure cookies included, empty when none match
String headerForURL(const URL&);
// keeps the Set-Cookie lines of a response, a redirect hop included
void receive(const WebCore::ResourceRequest&, const WebCore::CurlResponse&);
// the database file, read when the store is first used
void setStorePath(const std::string&);
// forgets every cookie of the profile
void clear();
// forgets the cookies of the private session
void clearPrivate();
// core thread only: refuses the cookies of other sites than the page's unless they already have some
void setThirdPartyBlocked(bool);

}
