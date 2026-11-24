# updates

## the check

`YZUpdate` (`app/YZUpdate.m`) reads the latest release of `sqmrak/yozora` through the stream proxy, so the chain is checked against the roots of the app and not the old trust store of ios 5 and 6. it asks `api.github.com/repos/sqmrak/yozora/releases/latest` first. an address gets 60 requests an hour there and the proxy turns every status it does not pass (429, 5xx) into a 502, so any answer that is not a 200 with a release makes it read `github.com/sqmrak/yozora/releases.atom` instead. the feed has no limit and carries the notes of the entry, so the changelog comes with either answer. the parsing is `core/yozora_release.c`: json and atom, markdown and html to plain text, version compare, the digest file. tests are `meow test release`.

- a draft or a prerelease is not a release for the person
- the tag takes letters, digits and `._-+`
- an asset address is taken only under `https://github.com/sqmrak/yozora/releases/download/`
- versions are compared by their first four numbers, the channel after them (`-stable`, `-developer`) is not compared. the update shows only when the release is newer, a build ahead of GitHub is left alone
- the last good answer is kept in the user defaults (`yozora.update.release`), so the screen has the version and the notes without a network

a release needs the tag `v<version>` and the assets `Yozora-v<version>.ipa` and `Yozora-v<version>.ipa.sha256` (`sha256sum` output or the 64 digits alone). a release without them is shown, and its button sends the person to the release page.

## where it shows

- every launch, after the start: when the release is newer, `YZUpdateCard` stands over the page with the standard sticker of the mascot, the installed and the newest version, the notes (seven lines) and the buttons. it is not remembered as dismissed, the next launch shows it again
- settings > Update: the installed version, the newest one, the button and the whole changelog. it asks again when the last check is older than two minutes, and has a button for another check

both are `YZUpdatePanel`. the modern design shows the progress as the wave bar with the pill handle of the player (`YZWaveSeekBar`) on a card of the surface color, the classic design as the glossy bar of ios 6 drawn by `YZUpdateProgress` on the lit plate, with lit keys.

## the install

- a phone with AppSync (`AppSyncUnified-installd.dylib` in the rootful or the rootless path) installs from the app, as App Installer does it. the ipa is downloaded through the proxy with its progress shown, its sha-256 is compared with the digest asset (a file without a match is deleted and not installed, a release without a digest is refused), then a manifest and the file are served from the loopback address of the proxy and `itms-services://?action=download-manifest` opens the manifest. the system asks, installs over the app and closes it. a background task and the idle timer keep the proxy alive while the system fetches the file. when the install fails the file stays and the card offers Open in…
- any other phone has no installer that takes an app signed by ldid, the button opens the release page in a new tab and the file comes by the downloads of the app

not tried on a device: that installd takes a manifest from the loopback address, and how far an ios 7 and later phone gets (the manifest rule for https there). the rest, the check, the parsers and the screens, compile on both slices.
