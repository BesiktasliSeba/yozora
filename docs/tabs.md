# tabs

## tray

the tab tray shows cards with a preview, a switch for normal and incognito tabs (each with its own list) and a floating button for a new tab. cards are kept between layout passes, reopening and resize: titles, previews and the selection are updated in place. closing a tab moves the remaining cards into the gap. while they move uikit blocks touches on them, so a hit area never differs from the picture. a button holds its tab and not its index, the index is found again when the animation ends, so a second close request cannot hit the wrong card.

## suspension

one completed tab stays in memory when another tab opens, with its forms, script state and navigation history. its tasks and loading are suspended and its raster pixels are dropped.

- loading pages, pages with native media, popups, pending authentication and a link held for another app are reloaded instead
- suspension is refused while indexeddb connections or open requests are alive, so a suspended request callback cannot hold a database lock in the shared server
- a memory warning evicts the suspended page, the interface does not wait for the core thread to do it

## previews

a picture of the page is taken a moment after it finishes loading, when the tray opens and when the app goes to the background. it is written to `Library/Caches/YozoraThumbnails/<tab id>.jpg`, tab ids are kept with the tab list, and read again after a memory warning or a restart. it is deleted with its tab. private tabs keep theirs in memory only. a tab without a preview shows a placeholder glyph. after a memory warning the tray lets go of the previews the model dropped, the image views do not keep them.

## restore

the tab list is saved when the app goes to the background and restored at start. private tabs are never written.
