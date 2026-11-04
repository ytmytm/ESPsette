import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const createPlaybackAwake = createRequire(import.meta.url)('../web/assets/playback-awake.js');

class Document extends EventTarget {
  visibilityState = 'visible';
  visibility(value) { this.visibilityState=value; this.dispatchEvent(new Event('visibilitychange')); }
}
class Lock extends EventTarget {
  released=false; releases=0;
  async release() { this.released=true; this.releases++; this.dispatchEvent(new Event('release')); }
}
class Video extends EventTarget {
  paused=true; plays=0; pauses=0; result=()=>Promise.resolve();
  play() { this.plays++; this.paused=false; return this.result(); }
  pause() { this.pauses++; const active=!this.paused; this.paused=true; if(active)this.dispatchEvent(new Event('pause')); }
  remove() { this.removed=true; }
}
function deferred() { let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no});return {promise,resolve,reject}; }
function environment(native, options={}) {
  const document=new Document(),window=new EventTarget(),statuses=[],locks=[];
  const video=new Video();let requests=0,created=0,clock=0;
  const navigator=native ? {wakeLock:{request(type){assert.equal(type,'screen');requests++;const lock=new Lock();locks.push(lock);return Promise.resolve(lock);}}} : {};
  const controller=createPlaybackAwake({document,window,navigator,now:()=>clock,
    createVideo(){created++;return video},onStatus:value=>statuses.push(value),...options});
  return {document,window,navigator,controller,video,locks,statuses,get requests(){return requests},get created(){return created},status:()=>statuses.at(-1),advance(ms){clock+=ms}};
}

// Only active playback requests a lock; repeated polls do not duplicate it.
{
  const e=environment(true),c=e.controller;
  await c.update(false);assert.equal(e.requests,0);
  await Promise.all([c.update(true),c.update(true)]);
  assert.equal(e.requests,1);assert.equal(e.status(),'native');
  await c.update(true);assert.equal(e.requests,1);
  await c.update(false);assert.equal(e.locks[0].released,true);assert.equal(e.status(),'idle');
  await c.update(true);assert.equal(e.requests,2);
  e.document.visibility('hidden');assert.equal(e.locks[1].released,true);assert.equal(e.status(),'hidden');
  await c.update(true);assert.equal(e.requests,2);
  e.document.visibility('visible');await c.update(true);assert.equal(e.requests,3);
  e.window.dispatchEvent(new Event('pagehide'));assert.equal(e.locks[2].released,true);
  e.window.dispatchEvent(new Event('pageshow'));await c.update(true);assert.equal(e.requests,4);
  c.dispose();assert.equal(e.locks[3].released,true);
  e.document.visibility('visible');await c.update(true);assert.equal(e.requests,4);
}
// A delayed grant must be released if playback stopped or the page was disposed.
for (const action of ['stop','hide','dispose']) {
  const e=environment(true),late=deferred();
  e.navigator.wakeLock.request=()=>late.promise;
  const pending=e.controller.update(true);await Promise.resolve();
  if(action==='stop')await e.controller.update(false);
  if(action==='hide')e.document.visibility('hidden');
  if(action==='dispose')e.controller.dispose();
  const lock=new Lock();late.resolve(lock);await pending;
  assert.equal(lock.released,true,action);
  assert.notEqual(e.status(),'native',action);
  e.controller.dispose();
}
// Stop/start while a request is pending must not retain a stale sentinel.
{
  const e=environment(true),late=deferred();
  const normal=e.navigator.wakeLock.request;
  e.navigator.wakeLock.request=()=>late.promise;
  const pending=e.controller.update(true);await Promise.resolve();
  await e.controller.update(false);
  const restarted=e.controller.update(true);
  const stale=new Lock();late.resolve(stale);await Promise.all([pending,restarted]);
  assert.equal(stale.released,true);
  e.navigator.wakeLock.request=normal;
  await e.controller.update(true);assert.equal(e.status(),'native');e.controller.dispose();
}
// Denial/system revocation is throttled, and a gesture can retry. No video bypass.
{
  const e=environment(true);let denied=0;
  e.navigator.wakeLock.request=()=>{denied++;return Promise.reject(new Error('power saving'))};
  await e.controller.update(true);assert.equal(e.status(),'blocked');
  for(let i=0;i<10;i++)await e.controller.update(true);
  assert.equal(denied,1);assert.equal(e.created,0);
  e.advance(15001);await e.controller.update(true);assert.equal(denied,2);
  const lock=new Lock();e.navigator.wakeLock.request=()=>Promise.resolve(lock);
  await e.controller.gesture();assert.equal(e.status(),'native');
  await lock.release();assert.equal(e.status(),'blocked');
  const next=new Lock();e.navigator.wakeLock.request=()=>Promise.resolve(next);
  await e.controller.gesture();assert.equal(e.status(),'native');e.controller.dispose();
}
// HTTP fallback is primed by an interaction, then runs only during playback.
{
  const e=environment(false),c=e.controller;
  await c.update(false);assert.equal(e.created,0);
  await c.gesture();assert.equal(e.video.plays,1);assert.equal(e.video.paused,true);assert.equal(e.status(),'idle');
  await c.update(true);assert.equal(e.video.plays,2);assert.equal(e.video.paused,false);assert.equal(e.status(),'media');
  await c.update(true);assert.equal(e.video.plays,2);
  e.video.dispatchEvent(new Event('pause'));assert.equal(e.status(),'media');
  await c.update(false);assert.equal(e.video.paused,true);
  await c.update(true);assert.equal(e.status(),'media');
  e.document.visibility('hidden');assert.equal(e.video.paused,true);assert.equal(e.status(),'hidden');
  e.document.visibility('visible');await c.update(true);assert.equal(e.status(),'media');
  e.video.pause();assert.equal(e.status(),'blocked');
  await c.gesture();assert.equal(e.video.paused,false);assert.equal(e.status(),'media');
  c.dispose();assert.equal(e.video.paused,true);assert.equal(e.video.removed,true);
}
// Embedded bytes become a Blob synchronously, are reused, then revoked on dispose.
{
  const blobs=[],revoked=[];
  const e=environment(false,{createVideo:undefined,
    mediaSources:[{src:'data:video/mp4;base64,AAH+/w==',type:'video/mp4'}],
    URL:{createObjectURL(blob){blobs.push(blob);return 'blob:local-test'},revokeObjectURL(url){revoked.push(url)}}});
  Object.assign(e.video,{style:{},setAttribute(){},canPlayType(){return 'probably'},load(){}});
  e.document.createElement=type=>{assert.equal(type,'video');return e.video};
  e.document.body={append(video){assert.equal(video,e.video)}};
  e.video.result=()=>{assert.equal(e.video.src,'blob:local-test');return Promise.resolve()};
  await e.controller.gesture();
  assert.equal(blobs[0].type,'video/mp4');
  assert.deepEqual(new Uint8Array(await blobs[0].arrayBuffer()),new Uint8Array([0,1,254,255]));
  await e.controller.update(true);assert.equal(e.status(),'media');
  await e.controller.update(false);assert.equal(e.video.paused,true);assert.equal(revoked.length,0);
  await e.controller.update(true);assert.equal(blobs.length,1);
  e.controller.dispose();assert.deepEqual(revoked,['blob:local-test']);
  e.controller.dispose();assert.equal(revoked.length,1);
}
// On touch devices, gesture priming must wait for an activation-triggering event.
{
  const e=environment(false);
  e.document.dispatchEvent(new Event('pointerdown'));assert.equal(e.video.plays,0);
  e.document.dispatchEvent(new Event('touchend'));await e.controller.gesture();
  assert.equal(e.video.plays,1);assert.equal(e.video.paused,true);
  await e.controller.update(true);assert.equal(e.status(),'media');e.controller.dispose();
}
// Fallback play() rejection and a late play() resolution cannot keep idle awake.
{
  const e=environment(false);e.video.result=()=>Promise.reject(new Error('gesture required'));
  await e.controller.update(true);assert.equal(e.status(),'blocked');assert.equal(e.video.plays,0);
  await e.controller.gesture();assert.equal(e.status(),'blocked');
  await e.controller.update(true);assert.equal(e.video.plays,1);
  e.video.result=()=>Promise.resolve();await e.controller.gesture();assert.equal(e.status(),'media');
  e.controller.dispose();
  const late=deferred(),other=environment(false);other.video.result=()=>late.promise;
  await other.controller.update(true);
  const pending=other.controller.gesture();
  await other.controller.update(false);late.resolve();await pending;
  assert.equal(other.video.paused,true);assert.equal(other.status(),'idle');other.controller.dispose();
}
// A browser that never settles play() must allow a retry after eight seconds.
{
  let timeout,cleared=0;
  const e=environment(false,{setTimeout(fn,ms){assert.equal(ms,8000);timeout=fn;return 1},clearTimeout(){cleared++}});
  const late=deferred();e.video.result=()=>late.promise;
  await e.controller.update(true);
  const pending=e.controller.gesture();assert.equal(e.status(),'requesting');
  timeout();await pending;
  assert.equal(e.status(),'blocked');assert.equal(e.video.paused,true);assert.equal(cleared,1);
  late.resolve();await Promise.resolve();assert.equal(e.video.paused,true);
  e.video.result=()=>Promise.resolve();await e.controller.gesture();assert.equal(e.status(),'media');
  e.controller.dispose();
}
// Stopping must also settle a hung attempt immediately, without waiting for timeout.
{
  const e=environment(false);e.video.result=()=>new Promise(()=>{});
  await e.controller.update(true);const pending=e.controller.gesture();
  await e.controller.update(false);await pending;assert.equal(e.status(),'idle');
  e.video.result=()=>Promise.resolve();await e.controller.update(true);await e.controller.gesture();
  assert.equal(e.status(),'media');e.controller.dispose();
}
console.log('Playback wake protection: native and HTTP fallback, lifecycle, denial, timeouts and pending-request races OK');
