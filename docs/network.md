# network

## stack

libcurl 8.23.0 over openssl 3.5.8, nghttp2 for http/2, brotli for `br`, libpsl for the public suffix list. the loader is `YozoraLoaderStrategy.cpp`, it replaces the platform loader of webcore.

## trust

- the roots are `app/ca-bundle.crt`: the mozilla bundle (curl.se `cacert.pem`) plus 7 older roots that mozilla no longer lists, because the trust store of an old system is stale
- the chain, the host name and the dates are always verified, there is no switch that turns it off and no second trust store
- a site signed by a root the bundle lacks fails with curl error 60 and shows an error page with the cause

## requests

- requests wait for a connection slot by webcore priority
- a request that webcore marks as blocked by the content blocker fails before a connection opens
- the user agent is iphone safari 18 with `Yozora/1` at the end, `yozora_user_agent()` owns it

## cookies

`cookies.db` in the profile. the setting for third party cookies uses the registrable domain of the request against the main document. in the private session cookies live in a second store in memory, see [incognito](incognito.md). `yozora_cookie_header` reads the header the store would send to an address, `HttpOnly` and `Secure` cookies included.

## http cache

responses up to 4 MB go to `Library/Caches/Yozora`, 64 MB, keyed by address. a page in the desktop mode neither reads nor writes it. history navigations may use any stored copy without asking the server. the private session has none. the format and its checks are `core/yozora_httpcache.c`, tested on the host.

## websockets

`ws` and `wss`: text and binary, blob sends, fragments, ping, close codes, cookies and origin on the handshake, `Set-Cookie` of the handshake stored, permessage-deflate with an 8 MB bound on the inflated size, workers, 8 MB per message. the stream thread reads until the socket has nothing more (up to 256 KB per pass) and hands it to the core thread once. a protocol error sends its close frame first, or ends after the 5 second close timeout when the queue cannot drain. `wss` verifies the certificate and the host name. a rule of the blocker can refuse a websocket.

## authentication

a 401 with basic or digest asks for a name and a password, kept for the run per origin and realm, cleared with the site data. only the page itself asks.

## downloads

a response that no viewer shows, or that the server marks as an attachment, is saved by its own curl request with the cookies of the page, three at a time. see [downloads](downloads.md).

## debug

`YOZORA_LOG_REQUESTS=1` prints every request and failure, blocked ones too.

## dns

the settings keep one DNS server (`YZDnsServer`), or none for the resolver of the system. libcurl here is built without c-ares, so `CURLOPT_DNS_SERVERS` does nothing; the server is asked over HTTPS with `CURLOPT_DOH_URL`, and curl gives its own request to the resolver the TLS options of the page request, so the chain, the name and the dates of the resolver are meant to be checked as for any site. that was read from curl and has not been run against a real resolver yet. `core/yozora_dns.c` turns what a person types into that address and is tested on the host: an https address is kept (host, optional port and path, no user name, query or fragment), a bare address or name becomes `https://NAME/dns-query`, the plain addresses of the four known resolvers become the name of the resolver (its certificate carries the name), an `http://` address is refused (the names would travel in the clear), and a name with no dot or with a number for a last label is refused. the process holds the server behind a lock (`yozora_dns_set`, `yozora_dns_get`) and `webkit-yozora-dns.patch` makes each curl request of WebCore take it when the request starts, so a change counts for the next request and not for those under way. the name of the resolver itself is looked up with the system resolver. the translation and update requests made by the app itself do not use it.
