# permissions

## site decisions

what the person decided for a site is kept in text files of the site data directory, one table per kind:

| kind | key | file |
|---|---|---|
| geolocation | origin | `geolocation.txt` |
| notifications | origin | `notifications.txt` |
| blocker paused | host | `adblock-sites.txt` |

a file is a header line that names the kind and its version, then `key<TAB>granted` or `key<TAB>denied`. a file with another header is not read, so a format that changes means the sites ask again.

- the table holds at most 512 sites, a new site past the limit replaces the one decided longest ago, a site decided again counts as the newest
- written beside the file and renamed over it, so a crash in the middle keeps the old table
- the origin `null` (a sandboxed frame, a local file) is the same text for every such page, so no decision is kept for it
- in the private session nothing is stored, the site asks again
- `core/yozora_sitegrants.c` is the table, `core/core/YozoraSites.cpp` guards it with a lock because the core thread and the interface both use it

the interface lists, removes and clears them (`yozora_site_*`): `Settings > Site permissions`, a segment for each kind, a swipe takes back one site, the bin takes back the list.

## geolocation

a page that calls `navigator.geolocation` puts a question in the core, the interface asks the person about the site and answers by id (`yozora_core_take_geolocation_request`, `yozora_core_geolocation_answer`). a site that has a decision is not asked again, the answer goes to the page at once.

while a page watches, the interface runs `CLLocationManager` (`app/YZLocation.m`) and passes the fixes on (`yozora_core_geolocation_position`). a value the system does not know is NaN, a position outside the globe or without an accuracy is not passed.

- ios 8 and later give no position until the app has asked for it, so the manager asks for permission while the app is in use (`NSLocationWhenInUseUsageDescription` is in `Info.plist`), the selector is looked up at run time
- the system hands over its last known fix first, and it can be hours old: a fix older than 30 seconds is dropped, so a page that asked now is not told the past
- a denial by the system, at the start or later, reaches the page as a permission error and stops the manager
- the manager stops when the app is hidden, because there is no location background mode, and starts again at the next frame when a page still watches
- high accuracy asks for the best the device has, otherwise a hundred meters with a 50 meter filter

a page on an insecure origin gets what webkit gives it. device orientation and motion are not done, webkit gets their data from the ios port only.

## notifications

a page that asks for the permission puts a question in the core, the decision is kept for the site. what a page shows is a banner for a few seconds. the core thread does not run while the app is hidden, so a page cannot show anything then.

## notifications of the app

downloads use the system, see [downloads](downloads.md).

## tests

`tests/test_sitegrants.c` runs on the host under asan and ubsan: the table, the limit, rejected keys, the file, other headers, missing files, long lines. `tests/test_core_geolocation.py` covers the core with a page that asks for a position, it needs `yozora-render`.
