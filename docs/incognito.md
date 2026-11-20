# incognito

a switch in the tab tray turns the session on. the menu of the private session has a "new tab" and a "leave incognito" entry, the menu of a normal session has none for a private tab, the switch of the tray is the way in.

## what stays out of the profile

while the session runs, `YozoraPrivateSession` is on and the core keeps in memory only:

- cookies, in a second store (`YozoraCookies::current()`)
- local storage, in a private namespace
- indexeddb, in a second in-memory hub
- stored responses: the http cache is off
- bytecode: no reads and no writes

all of it is dropped when the last private tab is closed (`yozora_clear_private_session`). the history is not written, private tabs are not written to disk and end with the app, their previews stay in memory. nothing is fetched for site icons. the profile's tabs stay parked and come back when the session is left.

## what it does not cover

downloaded files, bookmarks and the answers to http authentication are not part of the session.

## decisions of sites

geolocation, notifications and the pause of the blocker are not stored in the private session: the site asks again, and the menu offers no pause. see [permissions](permissions.md).

## look

the whole interface turns deep purple, with its own accent (`Settings > Private tab accent`, the same editor, the disc of the editor and the ball in the row change form and turn). the flag `YZSettings incognito` is not persistent and calls `yozora_set_private_session`.

switching in or out lays a picture of the screen over the interface while the tabs trade places and the palette changes, then it goes away through a circle that opens from the switch of the tray (or the menu button) on the emphasized curve of material 3, while the field, the glyphs, the tab cards and the start page wobble into place one after the other. with reduced motion the look just changes.
