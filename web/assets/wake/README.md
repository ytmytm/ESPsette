# Screen sleep protection fallback media

These tiny local MP4/WebM clips come from [NoSleep.js v0.12.0](https://github.com/richtr/NoSleep.js/tree/v0.12.0), under the bundled MIT license. `SOURCES.json` records their source and hashes. They were extracted from the upstream data URIs without transcoding.

The player uses native Screen Wake Lock where available. Ordinary HTTP ESP pages cannot use that secure-context API, so the player can instead loop one of these clips in a silent inline video during actual playback. A user interaction may be needed to unlock video playback. This fallback is browser-dependent and does not guarantee background execution or prevent manual device sleep. No external video service or network keepalive is used.

Firmware and the built static demo embed the clips as base64. The controller decodes the selected clip synchronously to a Blob URL before calling `play()` within the user's interaction, reuses that URL across STOP/PLAY, and revokes it on disposal. This avoids additional media requests while the ESP streams tape data and needs no LittleFS files. On the tested Mi Browser version, a video using a data URI directly never started; an HTTP URL played but flashed a buffering overlay; a Blob URL played without that overlay and kept the screen awake. When serving `web/` directly, the controller can also play the ordinary local media URLs.
