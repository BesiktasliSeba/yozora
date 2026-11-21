# ui

native uikit, objective-c with manual reference counting, one codebase from ios 5 to latest. what a system lacks is looked up at run time and has a fallback.

## look

material you, as in exteria.

- a tonal purple palette: light, dark, and black with its own grounds (`YZTheme`)
- material symbols rounded for glyphs, the search engines keep their own marks
- the address field is a pill search bar, the bottom bar grows a pill of the accent under a pressed button
- the menu is a compact card that grows out of the menu button and closes on a tap outside it
- tab cards have rounded corners and a floating button for a new tab
- settings, bookmarks, history and downloads: a large title that scrolls away, no lines between rows, cards whose corners follow the place of the row in its group, a glyph in a tonal disc, a material 3 switch with a thumb that wobbles over

## accent

`Settings > Accent color` opens the editor of weeabu and exteria: a card in the chosen color with its hex digits (a tap types them), tracks for hue, saturation and brightness, ten ready colors. the palette turns to the color the way exteria does it (`core/yozora_accent.c`): every hue moves by the distance of the accent from the purple of the palette (286 degrees), saturation follows, greys stay grey, brightness dims grounds and keeps words. the light theme shows the color itself as the accent, dark and black keep a lighter one of its hue. nothing is stored until a color is chosen.

## motion

the curves of material 3 and exteria (`core/yozora_motion.c`): emphasized decelerate and accelerate for sheets and menus, and a spring that passes its target by a sixth and swings back twice for cards, tiles, glyphs, banners and the tab switch. the spring is sampled as keyframes, so ios 5 has it too. press feedback is a bounce. everything follows reduce motion where the system has it, and the settings have an own switch.

## bars

on a phone the bottom bar leaves when the page has been read down by a few finger lengths and comes back only at the top of the page, while the address is typed and whenever a part of the interface opens. a scroll up in the middle of a page does not bring it back.

## controls of pages

the core asks, the interface answers by id:

- a `select` opens as a material 3 bottom sheet: group labels, unavailable items, a check on the chosen one. with `multiple` it is a list with a check for each item and a done button, the core takes the tap before the page does
- date, time, datetime-local, month and week fields open the wheels of `UIDatePicker` in a sheet, the value goes back in the format of the field and the limits of the page apply
- `<input type=color>` opens a card with the color and its hex digits and tracks for hue, saturation and value (`YZColorPicker`, the math in `core/yozora_color.c`)
- a file field offers the photo library, the downloads folder and, on ios 8 and later, the files app. a photo is written as a jpeg to `tmp/YozoraUploads`, files older than a day are removed with the next photo. a file field takes one file from the downloads folder, several only from the photos and the files app
- long press on a link or an image opens a sheet: new tab, private tab (not inside the private session), copy link, share link, and for an image open, copy address, share address
- share and copy: the menu hands the address to the share sheet, `Copy address` says so in a banner. the share sheet needs ios 6, an older system shows no share entries

## find in page

the menu replaces the address bar with a field, a count ("3/12", at most 500 are counted) and arrows. every match is marked by webcore, the current one is selected and scrolled into view, the search wraps and ignores case.

## zoom

two fingers enlarge the picture of the page the way safari does, the layout keeps its width (scale 1 to 3, one finger moves the view over the page and scrolls past its edge). when the pinch settles on a phone with 900 MB or more the tiles are painted again at up to twice the pixels, so text stays sharp. a phone with less enlarges the tiles it has. a new page starts at scale 1, a keyboard keeps the zoom, a rotation drops it. the math is `core/yozora_zoom.c`.

## desktop site

the core keeps it (`yozora_core_set_desktop`): the user agent of safari on a mac and a layout 980 points wide. the menu has no entry for it, the interface cannot switch it on. the mode belongs to the tab and a page in it neither reads nor writes the disk cache.

## ipad

all four orientations. a window of 700 points or more shows the tabs as a strip above one toolbar (back, forward, address, bookmark, tabs, menu), the bottom bar is gone and the menu hangs from the toolbar. a narrower window (slide over, half of a split view) keeps the phone layout. split view and slide over need ios 9 and a launch storyboard.

## start

the system shows a violet launch screen, then `YZSplashController` (the same violet, the sticker that wobbles in and the name) while the browser, the core and the tabs are made on the next turn of the run loop, and the splash fades into the browser. a link that starts the app waits for the browser.

the start page shows the sticker and the name above the tiles. in the private session it shows a note about the session.

## site icons

tiles, rows of bookmarks and history, and tab cards show the `favicon.ico` of the site over https with the system trust store, kept in `Library/Caches/YozoraIcons` and asked again after three days when a site has none. nothing is fetched in the private session. a site without a usable icon keeps its colored letter tile.

## default browser

`Info.plist` declares `http` and `https` and the entitlement `com.apple.developer.web-browser` is signed in, so ios 14 and later list yozora under the default browser setting and links from other apps open in it. the scheme `yozora-auth` is declared too, it is the entry for the sign in of another app, see [signin](signin.md).


## links to other apps

a link the person followed, or a button that sets `location` in its click handler, is offered to the other apps before the page navigates. scripts without a gesture, history steps and reloads are never offered.

- a scheme of another app (`tg:`, `mailto:`, `tel:`, `itms-apps:`) is dropped by the page and opened with `openURL`. when nothing can open it a banner says so
- an http or https address of another site is a universal link candidate. the page waits while the app asks the system to open it with `UIApplicationOpenURLOptionUniversalLinksOnly`, which succeeds only for an app that claims the address. on success the page stays, on failure the page loads the address as before
- universal links exist on ios 10 and later. ios 5 to 9 answer no at once, because `openURL:` on an https address would give the page to Safari
- an address on the host of the page (ignoring `www.`), a frame inside the page for universal links, addresses with a user name or password, and the schemes `javascript`, `data`, `file`, `blob`, `about`, `view-source`, `ws`, `wss`, `ftp`, `filesystem` and `itms-services` are never offered
- a held universal link is answered with an ignore when the page stops, loads something else, is destroyed or leaves the screen, so no navigation waits for ever
- the rule is `yozora_address_external` in `core/yozora_address.c`, tested on the host in `tests/test_address.c`. the hand over is `core_offer_external` in `core/core/yozora_core.cpp`, the option `external_links` turns it on and the headless runner leaves it off

## translation

the menu offers to translate the page (`core/yozora_translate.c`, the library `yozora_translate.js` runs in an isolated world). the target language is the language of the device until the person picks one, languages can be never offered or always translated. the text of a private tab goes to the service only when the person asks.

## languages

the interface is available in russian, english and japanese. choose a language in settings under general. until a language is chosen, the app uses the first language of the device if supported, or english otherwise.

## error pages

a failed load shows the address, the cause and a retry button in the page area. dns, connection, timeout, tls and interrupted transfers have their own messages, and the technical error stays visible below them.

## reuse

cards, tiles and rows are reused across layout, resize and data updates and not rebuilt: tab cards keep their objects, closing a tab moves the others into the gap, home tiles keep their buttons and get new targets. entrances are limited to eight visible items and 0.36 s. see [performance](performance.md).
