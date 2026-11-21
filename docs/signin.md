# sign in for another app

an app that needs a person to sign in on a site can hand that to yozora and get the result back, without a web view of its own. the sign in runs in a private tab, so the cookies of the profile stay out of it.

## the request

```
yozora-auth://start?url=URL&callback=CALLBACK&until=PREFIX&cookie=NAME&state=TEXT
```

every value is percent-encoded, a plus stays a plus. the scheme is declared in `Info.plist`. an app that wants to know whether yozora is installed lists `yozora-auth` in `LSApplicationQueriesSchemes`.

- `url` (required): the page to open, http or https, with a host and no credentials
- `callback` (required): a link of the caller, with a scheme (letters, digits, `+`, `-`, `.`, starting with a letter) and no fragment. the answer is appended to it as query fields, after a `?` or a `&` that the callback already ends with
- `until`: the sign in ends when the address of the page starts with this, byte for byte. give a full prefix: `https://a.com/cb` also ends at `https://a.com/cbx`. it must not already be a prefix of `url`
- `cookie`: or when the cookies the page would send include a cookie of this name with a value. the cookies are read about once a second while the tab is shown
- `state`: returned as it came, for the caller to match the answer to its request
- at least one of `until` and `cookie` is required, either one ends the sign in
- a field that comes twice, a bad escape, a control byte or a value over its cap (2048 bytes for an address, 256 for the callback and the state, 128 for the cookie name) refuses the whole request. unknown fields are ignored

schemes of pages and of the system cannot be a callback: `http`, `https`, `javascript`, `data`, `file`, `blob`, `about`, `view-source`, `ws`, `wss`, `ftp`, `filesystem`, `itms-services`, `mailto`, `tel`, `sms`, `facetime` and `yozora-auth` itself.

## what the person sees

yozora asks first: "the app `scheme:` asks to open HOST in a private tab and give it the page address and cookies when the sign in ends". any app can send the link, and the answer carries cookies, so the question names the scheme that the answer goes to. a cancel is answered with `status=cancelled`.

on yes the private session is turned on if it is off, a blank private tab is made (a full tray refuses, and the caller hears `cancelled`), and the page loads. when the end condition holds the tab is closed, yozora goes back to the normal tabs if the sign in switched them, and the callback is opened. closing the tab before the end answers `cancelled`. a new request replaces a sign in that is still running without an answer to the first.

## the answer

the callback with:

- `status`: `done`, `cancelled`, or `too_large` when the cookies did not fit the answer (32768 bytes, a cookie header over 16384 bytes is too large before it is encoded)
- `url`: the address of the page when the sign in ended
- `cookies`: the `Cookie` header the page would send to that address, `name=value; name=value`, HttpOnly and Secure cookies included. only the cookies that match that address, no attributes
- `state`: as it came

a cancelled answer has no cookies. `too_large` has the address and no cookies.

the callback link is opened with `openURL` (`openURL:options:completionHandler:` where it exists).

## what it does not do

- the tab is private, not a separate session: cookies of other private tabs that are open at the same time are in the same store and can match
- cookies of other hosts than the final page (a login host that differs from the app host) are not in the answer
- anything that opens a link with the callback scheme can receive the answer: ios has no way to prove which app owns a scheme. the question of the person is the only check
- the cookie of a request that ends by `cookie` is looked at while the sign in tab is the tab on the screen
- there is no timeout: a request waits until the tab ends or closes
- a callback link that ios cannot open shows "could not go back to the app"

## where it is

- `core/yozora_signin.c`: parsing, the end check and the answer, plain c with a host test (`tests/test_signin.c`)
- `core/core/yozora_core.cpp`: `yozora_cookie_header`, reads the store of the moment (the private one while the session is on) on the core thread
- `app/YZSignIn.m`: the request as an object, reading cookies off the main thread
- `app/YZBrowserController.m`: the question, the private tab, the check of every frame, closing and the return
