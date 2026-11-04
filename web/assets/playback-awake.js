/* Playback-scoped screen sleep protection. The media fallback clips are
 * from NoSleep.js (MIT); see assets/wake/LICENSE.txt. No upstream JS is used. */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else root.createPlaybackAwake = factory;
})(typeof globalThis !== 'undefined' ? globalThis : this, function (options) {
  const doc = options.document, nav = options.navigator || {};
  const now = options.now || Date.now;
  const schedule = options.setTimeout || setTimeout, unschedule = options.clearTimeout || clearTimeout;
  const objectURLs = options.URL || URL;
  const report = options.onStatus || function () {};
  const native = typeof nav.wakeLock?.request === 'function';
  let wanted = false, suspended = false, disposed = false, generation = 0;
  let lock = null, pending = null, video = null, unlocked = false;
  let mediaRunning = false, retryAt = 0, status = '', reason = '';
  let cancelMedia = null;
  let mediaURL = null;
  const visible = () => !suspended && doc.visibilityState !== 'hidden';
  function notify(value, detail = '') {
    if (value !== status || detail !== reason) { status = value; reason = detail; report(value, detail); }
  }
  function describe(error) { return error ? [error.name, error.message].filter(Boolean).join(': ') : 'Unknown browser error'; }
  function release(sentinel) {
    try { Promise.resolve(sentinel.release()).catch(function () {}); } catch (_) {}
  }
  function stop() {
    generation++;
    if (cancelMedia) cancelMedia();
    if (lock) { const previous = lock; lock = null; release(previous); }
    if (video) video.pause();
    mediaRunning = false;
  }
  function createVideo() {
    if (options.createVideo) return options.createVideo();
    const media = doc.createElement('video');
    // The bundled audio samples are silent. Setting muted breaks the wake
    // effect on some iOS versions; playback is unlocked by a real tap instead.
    media.muted = false; media.loop = true; media.playsInline = true;
    media.preload = 'auto'; media.disablePictureInPicture = true;
    media.disableRemotePlayback = true;
    media.setAttribute('playsinline', ''); media.setAttribute('aria-hidden', 'true');
    media.tabIndex = -1;
    media.style.cssText = 'position:fixed;width:1px;height:1px;left:0;bottom:0;opacity:.01;pointer-events:none';
    // Mi Browser hangs on a video data URI, but plays the same bytes from a
    // Blob URL without its HTTP buffering overlay. Decode synchronously so
    // play() still runs inside the user's activation event.
    const sources = options.mediaSources || [];
    const source = sources.find(item => media.canPlayType(item.type)) || sources[0];
    if (source) {
      const embedded = /^data:([^;,]+);base64,(.*)$/.exec(source.src);
      if (embedded) {
        const bytes = Uint8Array.from(atob(embedded[2]), character => character.charCodeAt(0));
        mediaURL = objectURLs.createObjectURL(new Blob([bytes], {type:embedded[1]}));
      }
      media.src = mediaURL || source.src;
    }
    doc.body.append(media);
    media.load();
    return media;
  }
  function ensure(prime = false) {
    if (disposed || !visible() || (!wanted && !prime)) return Promise.resolve();
    if (lock && !lock.released) { notify('native'); return Promise.resolve(); }
    if (mediaRunning && wanted) { notify('media'); return Promise.resolve(); }
    if (pending) return pending;
    if (!prime && now() < retryAt) return Promise.resolve();
    const token = generation;
    if (native) {
      if (!wanted) return Promise.resolve();
      notify('requesting');
      pending = Promise.resolve().then(() => nav.wakeLock.request('screen')).then(sentinel => {
        if (disposed || token !== generation || !wanted || !visible()) { release(sentinel); return; }
        if (sentinel.released) { retryAt = now() + 15000; notify('blocked', 'Browser returned an already released wake lock.'); return; }
        lock = sentinel;
        sentinel.addEventListener('release', () => {
          if (lock !== sentinel) return;
          lock = null;
          if (wanted && visible() && !disposed) { retryAt = now() + 15000; notify('blocked', 'Browser released the screen wake lock.'); }
        });
        notify('native');
      }).catch(error => {
        if (token === generation && wanted && visible() && !disposed) {
          retryAt = now() + 15000; notify('blocked', describe(error));
        }
      }).finally(() => { pending = null; });
    } else {
      if (!prime && !unlocked) {
        notify('blocked', 'A tap is required to enable silent video on HTTP.');
        return Promise.resolve();
      }
      if (prime && unlocked && !wanted) return Promise.resolve();
      let play;
      try {
        if (!video) {
          video = createVideo();
          const stopped = event => {
            if (!mediaRunning) return;
            // Mi Browser emits pause around loop boundaries even though the
            // element has already resumed by the time this event is handled.
            if (event.type === 'pause' && !video.paused) return;
            mediaRunning = false;
            if (wanted && visible() && !disposed) {
              retryAt = now() + 15000;
              notify('blocked', 'Video ' + event.type + (video.error ? ': ' + video.error.code + ' ' + video.error.message : ''));
            }
          };
          video.addEventListener('pause', stopped);
          video.addEventListener('ended', stopped);
          video.addEventListener('error', stopped);
        }
        play = video.play();
      }
      catch (error) { if (wanted) { retryAt = now() + 15000; notify('blocked', describe(error)); } return Promise.resolve(); }
      if (wanted) notify('requesting');
      // Some mobile browsers leave play() pending indefinitely, without an
      // error event. Bound the wait and cancel it when playback stops.
      let expired = false, timer;
      const started = new Promise((resolve, reject) => {
        cancelMedia = () => { expired = true; reject(new Error('Video request cancelled.')); };
        timer = schedule(() => {
          expired = true;
          video.pause();
          if (video.load) video.load();
          reject(new Error('Silent video did not start within 8 seconds. Try Chrome or HTTPS.'));
        }, 8000);
        Promise.resolve(play).then(() => {
          if (expired) { if (!mediaRunning) video.pause(); return; }
          resolve();
        }, reject);
      });
      pending = started.then(() => {
        unlocked = true;
        if (disposed || token !== generation || !wanted || !visible()) { video.pause(); return; }
        mediaRunning = true; notify('media');
      }).catch(error => {
        if (token === generation && wanted && visible() && !disposed) {
          retryAt = now() + 15000; notify('blocked', describe(error));
        }
      }).finally(() => { unschedule(timer); cancelMedia = null; pending = null; });
    }
    return pending;
  }
  function update(active) {
    const next = !!active;
    if (next !== wanted) { wanted = next; retryAt = 0; generation++; }
    if (!wanted || !visible()) {
      stop(); notify(wanted ? 'hidden' : 'idle'); return Promise.resolve();
    }
    return ensure();
  }
  function gesture() {
    if (disposed || !visible()) return Promise.resolve();
    retryAt = 0;
    return ensure(true);
  }
  function visibility() {
    if (!visible()) { stop(); notify(wanted ? 'hidden' : 'idle'); }
    else { retryAt = 0; if (wanted) ensure(); }
  }
  function hide() { suspended = true; visibility(); }
  function show() { suspended = false; visibility(); }
  const host = options.window;
  doc.addEventListener('visibilitychange', visibility);
  doc.addEventListener('click', gesture, true);
  doc.addEventListener('touchend', gesture, true);
  doc.addEventListener('keydown', gesture, true);
  if (host) { host.addEventListener('pagehide', hide); host.addEventListener('pageshow', show); }
  notify('idle');
  return {
    update, gesture,
    dispose() {
      disposed = true; wanted = false; stop();
      doc.removeEventListener('visibilitychange', visibility);
      doc.removeEventListener('click', gesture, true);
      doc.removeEventListener('touchend', gesture, true);
      doc.removeEventListener('keydown', gesture, true);
      if (host) { host.removeEventListener('pagehide', hide); host.removeEventListener('pageshow', show); }
      if (video && video.remove) video.remove();
      if (mediaURL) { objectURLs.revokeObjectURL(mediaURL); mediaURL = null; }
    }
  };
});
