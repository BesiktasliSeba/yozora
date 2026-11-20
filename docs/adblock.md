# adblock

## what it checks

every subresource request of a page, in the loader (`YozoraLoaderStrategy.cpp` calls `YozoraAdblock`), and every websocket. a page the person opens by address is never blocked, only what it pulls in. a blocked request fails before a connection opens.

a request is judged by:

- its url and host
- its type: document of a frame, script, image, stylesheet, font, media, `xmlhttprequest` (fetch and xhr load as raw resources, a page cannot tell them apart), ping, websocket, other
- third party or not: the registrable domain of the request against the one of the top document, a request with no top document (a worker) is not called third party
- the host of the page

## rules

two layers, the second wins:

1. the hosts of ad networks and trackers, `core/yozora_blocklist.c`, a sorted table matched by label from the right
2. rules in the syntax of adblock plus lists, `core/yozora_filter.c`, with the shipped ones (a few address patterns of ad and statistics calls, the markers of ad slots) and the ones the person wrote

an exception from the second layer lets a request through even when the first blocked it.

### syntax

- `||example.com^`: the host and its subdomains
- `|https://a/path`: the address starts with it, a trailing `|` ends it
- `/ads/ban*er^`: text, `*` for any run, `^` for a separator (not a letter, digit, `_`, `-`, `.`, `%`) or the end
- `@@rule`: an exception
- options after `$`: `third-party`, `~third-party`, `script`, `image`, `stylesheet`, `font`, `media`, `xmlhttprequest`, `subdocument`, `ping`, `websocket`, `other`, any with `~`, `domain=a.com|~b.com`. `important` and `match-case` are accepted and ignored
- `@@||site^$document`: the blocker is off for that site, requests and hiding
- `##selector`: hides the elements everywhere, `a.com,b.com##selector` on those sites and their subdomains, `~a.com` leaves one out, `a.com#@#selector` lets a hidden selector stay on a site
- `!` starts a comment, a line starting with `[` is a header

### unsupported

counted and not used: regular expressions, `$csp`, `$redirect`, scriptlets, extended selectors (`#?#`), procedural selectors (`:has-text`, `:xpath`, `:matches-css` and the like). the editor shows how many lines were taken, skipped, unsupported and refused.

a rule of nothing but wildcards is refused, it would match every address.

## hiding

hiding rules become one user style sheet, one rule per selector so a selector that the engine cannot parse drops only itself. the sheet is given to the content provider of the page, so every document and every frame gets it, and it is replaced when the main frame commits a page of another site.

a selector with `{`, `}`, `<`, a comment opener or a control character is refused, so a rule cannot break out of its rule.

## limits

- 4000 network rules, 1000 hiding rules, 256 KB of text, a line of at most 1024 bytes, a pattern of at most 512
- only the first 2048 bytes of an address are matched
- no filter lists are downloaded

## cost

a request first fills a bit table with the trigrams of its address, and a rule is tried only when the table has the first trigram of its longest literal. 3500 rules cost 14 microseconds a request on the host, 190 without the table.

## per site

the menu shows `blocker on this site: N` with the count of requests stopped on the page, and pauses the blocker for the site. a pause covers the site and its subdomains, `www.` is dropped from the key, a host of one label (localhost) has none. the private session offers no pause and stores none. the list of paused sites is in `Settings > Site permissions`.

## settings

- the switch for the whole blocker is `Block ads and trackers`
- `Blocker rules` is the editor: a text, applied when the screen is left or the check mark is pressed, kept in `Library/Yozora/adblock-rules.txt`

## not checked

media played by the system player and requests that carry no top document (workers).

## tests

`tests/test_blocklist.c` and `tests/test_filter.c` run on the host under asan and ubsan: anchors, wildcards, separators at the end, options, exceptions, site exceptions, the shipped rules, unsupported and invalid lines, hiding with exceptions and a cut sheet, limits, bounds of a request, ports and user information.
