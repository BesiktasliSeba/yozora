# core

## one thread

one thread owns webcore for the whole process. the interface thread never touches it: calls that change a page queue a task and return, state comes from a copy the core thread publishes after every pass.

between tasks the thread sleeps in the webcore run loop. page timers and network callbacks run there, independent of rendering.

## passes

- rendering uses deadlines 16,667 microseconds apart, a late pass skips the deadlines it missed
- a quiet page publishes its state every 100 ms, a rendering invalidation or an interface command brings the next pass forward
- an unchanged page skips layout and recording, raster work never enters webcore
- a repeated viewport size keeps the recordings and the raster cache
- the interface polls a quiet page every two display frames and a moving one at display rate, and applies only the layer properties that changed
- dragging and flinging do not suspend timers, animation frames or queued tasks, there is no separate throttle on layout while a page loads

## the api

`core/core/yozora_core.h` is the whole surface, plain c:

- life: create, destroy, load, reload, stop, resize, set options
- input: tap, touch, scroll, fling, select, long press, find
- state: url, title, progress, load state, page error, the newest scene
- questions the page asks the person, taken by the interface and answered by id: `take_choice`, `take_files`, `take_date`, `take_auth`, `take_geolocation_request`, `take_notification_request`, `take_media`, `take_popup`
- links for other apps: `take_external` hands over the address, `answer_external` tells the page whether an app opened a universal link
- downloads, site data, caches, the blocker, site decisions
- `yozora_cookie_header`: the `Cookie` header the current store would send to an address, for the sign in of another app, see [signin](signin.md)

a question carries an id, the interface shows its own control and answers with the id. a queue that is full refuses at once, so a page that asks in a loop gets a denial and not a silence.

## scripts that hold the thread

a script that keeps the thread for 8 seconds is offered to be stopped (`yozora_stop_scripts`, callable from any thread), after a memory warning the wait is 4 seconds. nothing ends a script on a timer by itself and no question is asked about a slow script. a person who leaves the page (new address, back, reload, stop, closing the tab) ends its script first, because the request would otherwise wait behind it.

## memory warnings

the interface never waits for the core thread to handle one:

- the cleanup is queued, repeated pending warnings share one cleanup
- deleting a parked tab hands the page to the core thread and returns
- critical release does one full script collection, the bytecode writer has its own thread
- pictures of other tabs are dropped at once on the interface side
- a script that holds the thread is stopped before the cleanup is queued, so the interface stays able to react

forms, indexeddb, cookies and history are never cleared to free memory.

## input

- a one finger drag reaches the page as `touchstart`, `touchmove` and `touchend`, with no mouse events so nothing is selected. when the page calls `preventDefault` the interface stops scrolling and flinging under the finger
- taps stay mouse events
- a finger held on words selects the word, on a link or an image opens its sheet, on anything else it is a slow tap and the release clicks
- a block with overflow `auto` or `scroll` and a text area scroll under the finger before the page does
- two fingers zoom the picture, see [ui](ui.md)

## indexeddb and storage

indexeddb runs on a thread of its own in the process, its databases go with the site data. local storage is kept as a text file, 5 MB per origin. a 401 with basic or digest asks for a name and a password, kept for the run per origin and realm and cleared with the site data, only the page itself asks and not its pictures.
