#pragma once

#include <pgmspace.h>

// Generated from web/player.html by scripts/embed-player.py. Do not edit here.
static const char PLAYER_PAGE[] PROGMEM = R"PAGE(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>espsette</title>
<style>
:root{color-scheme:dark;font:16px system-ui}*{box-sizing:border-box}body{margin:0;background:#11151b;color:#edf0f2}
main{max-width:760px;margin:auto;padding:18px}h1{font-size:1.6rem;margin:0 0 4px}p{line-height:1.45;color:#abb7c3}
.deck{background:linear-gradient(135deg,#373d44,#1d232b 58%,#353d42);border:3px solid #60666b;border-radius:20px;padding:20px;box-shadow:0 22px 45px #0008,inset 0 2px 4px #ffffff33}
.deck-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;color:#b9c2c7;font-size:.75rem;font-weight:700;letter-spacing:.18em}.counter{display:flex;gap:2px;padding:3px 5px;background:linear-gradient(#65645e,#c2bdad 25%,#948f81 80%,#3d3b36);border:2px solid #222326;border-radius:4px;box-shadow:inset 0 2px 5px #0009,0 1px 1px #ffffff50;font:700 1.45rem/1.15 ui-monospace,monospace;color:#101010;font-variant-numeric:tabular-nums}.counter-digit{display:block;width:.69em;height:1.15em;overflow:hidden;background:linear-gradient(#aaa69a,#e4dfcf 35%,#e2ddce 65%,#928e84);border:1px solid #5f5c53;border-radius:2px;text-align:center;box-shadow:inset 0 1px 2px #0005}.counter-strip{display:block}.counter-strip span{display:block;height:1.15em}.counter-strip.rolling{animation:counter-roll .42s ease-out both}@keyframes counter-roll{from{transform:translateY(0)}to{transform:translateY(-50%)}}
.window{height:clamp(190px,34vw,275px);border:10px solid #141a20;border-radius:12px;background:linear-gradient(145deg,#090c10,#20252c);box-shadow:inset 0 5px 15px #000;display:flex;align-items:center;justify-content:center;overflow:hidden;position:relative}
.cassette{width:min(100%,570px);aspect-ratio:720/314;background:url('data:image/webp;base64,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') center/contain no-repeat;position:relative;filter:drop-shadow(0 5px 8px #0009)}
.reel{position:absolute;width:9%;aspect-ratio:1;border-radius:50%;background:repeating-conic-gradient(#cabfa8 0 12deg,#443d37 12deg 38deg,#e7dcc6 38deg 55deg,#292522 55deg 60deg);box-shadow:inset 0 0 0 6px #292522,0 0 3px #000;transform:translate(-50%,-50%);left:29.9%;top:48.4%}
.reel.right{left:70.3%;top:48.4%}
.playing .reel{animation:spin 1.6s linear infinite}@keyframes spin{from{transform:translate(-50%,-50%) rotate(0)}to{transform:translate(-50%,-50%) rotate(360deg)}}
.ejected .cassette{display:none}.ejected .window:after{content:'EMPTY DECK';color:#9aa6b2;letter-spacing:.2em}
.keys{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:6px;margin-top:15px}.key{margin:0;font:700 clamp(.66rem,2vw,.9rem) system-ui;background:linear-gradient(#34393d,#0c1015);border:2px solid #535a62;border-radius:5px 5px 10px 10px;min-height:52px;padding:8px 3px;box-shadow:0 5px 2px #080a0d;color:#d2dce3;cursor:pointer}.key.pressed{transform:translateY(4px);box-shadow:0 1px 2px #080a0d;color:#fff}.key:disabled{cursor:not-allowed;opacity:.45}.key:active{transform:translateY(4px);box-shadow:0 1px 2px #080a0d}.autoplay{margin:10px 0 0;color:#dce5e9;font-size:.9rem}.autoplay input{accent-color:#e1b54c;width:1.1rem;height:1.1rem;vertical-align:middle}#deckMessage{margin:12px 0 0;min-height:1.5em;text-align:center;font-size:.9rem}.key:focus-visible,button:focus-visible,summary:focus-visible{outline:3px solid #f4c354;outline-offset:2px}
.debug{margin-top:12px;border-top:1px solid #42505d;padding-top:10px}.debug summary{cursor:pointer;color:#d7e0e8}.debug pre{white-space:pre-wrap;overflow-wrap:anywhere;font:12px ui-monospace,monospace;color:#b8c9d8;background:#10171e;border-radius:8px;padding:12px;max-height:330px;overflow:auto}
@media(max-width:480px){main{padding:12px}.deck{padding:11px}.keys{gap:3px}.key{min-height:46px;padding:5px 2px}}
@media(prefers-reduced-motion:reduce){.playing .reel{animation-duration:5s}.counter-strip.rolling{animation:none;transform:translateY(-50%)}}
.card{background:#202832;border:1px solid #36414b;border-radius:12px;padding:16px;margin-top:18px}
label{display:block;margin:12px 0}input[type=file],select{display:block;width:100%;margin-top:8px}select{font:inherit;padding:10px;background:#111923;color:#fff;border:1px solid #566472;border-radius:8px}.drop{border:2px dashed #687786;border-radius:10px;padding:18px;text-align:center;color:#b9c7d3}.drop.over{border-color:#e2b94d;background:#39414a}button{font:inherit;border:0;border-radius:8px;background:#d5a43a;color:#17191a;padding:10px 15px;margin:4px 5px 4px 0;font-weight:700}button.secondary{background:#414e5b;color:#fff}button:disabled{opacity:.45}a{color:#9fcfff}
.stats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.stats div{background:#131a22;border-radius:7px;padding:12px}.stats b{display:block;font-size:1.2rem;overflow-wrap:anywhere}small{color:#a4b3bf}#error{color:#ffaaa6;min-height:1.3em}
.scope-wrap{margin-top:14px;background:#0c1117;border:1px solid #2a3440;border-radius:8px;padding:10px 10px 8px}
.scope-wrap canvas{display:block;width:100%;height:72px;image-rendering:pixelated}
.scope-wrap small{display:block;margin-top:6px}
</style>
<main><h1>espsette</h1><p>Tape over Wi-Fi · C64 / C16 PAL</p>
<div id="deck" class="deck"><div class="deck-top"><span>DATASETTE</span><output id="counter" class="counter" aria-label="Tape counter 000"><span class="counter-digit"><span id="counterStrip0" class="counter-strip"><span id="counterOld0">0</span><span id="counterNext0">0</span></span></span><span class="counter-digit"><span id="counterStrip1" class="counter-strip"><span id="counterOld1">0</span><span id="counterNext1">0</span></span></span><span class="counter-digit"><span id="counterStrip2" class="counter-strip"><span id="counterOld2">0</span><span id="counterNext2">0</span></span></span></output></div><div class="window"><div class="cassette" id="cassette"><div class="reel" id="reelLeft"></div><div class="reel right" id="reelRight"></div></div></div><div class="keys" role="group" aria-label="Datasette controls"><button id="rewind" class="key" type="button" aria-label="Previous tape position">◀◀<br>REW</button><button id="stop" class="key" type="button" aria-label="Release PLAY on SENSE">■<br>STOP</button><button id="play" class="key pressed" type="button" aria-label="Press PLAY on SENSE">▶<br>PLAY</button><button id="fastForward" class="key" type="button" aria-label="Next tape position">▶▶<br>FF</button><button id="deckEject" class="key" type="button" aria-label="Eject tape">⏏<br>EJECT</button></div><p id="deckMessage" role="status" aria-live="polite"></p><label class="autoplay"><input id="autoPlay" type="checkbox" checked> Autoplay when tape is inserted</label></div>
<section class="card"><div class="stats"><div><small>Tape</small><b id="loaded">—</b></div><div><small>Motor</small><b id="motor">—</b></div><div><small>Played</small><b id="played">—</b></div><div><small>ESP buffer</small><b id="buffer">—</b></div><div><small>Signal remaining</small><b id="remaining">—</b></div><div><small>WRITE / SAVE</small><b id="writeStatus">—</b></div></div><div class="scope-wrap"><canvas id="scope" width="640" height="72" aria-label="READ waveform"></canvas><small id="scopeLabel">READ · waiting for playback</small></div><p id="writeDetail"></p><p id="phase">Connecting…</p><p id="transport"></p><p id="error"></p><details id="debugPanel" class="debug"><summary>Debug / ESP state</summary><p id="diagnostics"></p><pre id="debugJson"></pre></details></section>
<section class="card"><label>Default computer for PRG<select id="defaultMachine"><option value="c64">C64</option><option value="c16">C16 / Plus/4 PAL</option></select></label><p>A TAP identifies its computer in the header. The built-in demo is for C64.</p></section>
<section class="card"><div id="dropzone" class="drop">Drop a TAP or PRG; optionally add an IDX for TAP</div><p id="selectedFiles"></p><label>C64 / C16 PAL TAP v1–v2 or PRG<input id="file" type="file" accept=".tap,.prg"></label><div id="indexPicker"><label>IDX index (optional for TAP)<input id="indexFile" type="file" accept=".idx,text/plain"></label><button id="clearIndex" class="secondary">Clear IDX selection</button></div><div id="prgOptions" hidden><label>Computer<select id="machine"><option value="c64">C64</option><option value="c16">C16 / Plus/4 PAL</option></select></label><label>Prepend turbo loader<select id="turbo"><option value="0">No — standard LOAD</option><option value="1" disabled>Yes — coming later</option></select></label><label>Tape name<input id="programName" maxlength="16" placeholder="PROGRAM"></label></div><button id="load">Insert tape</button><button id="demo" class="secondary">Demo tape</button><div id="indexControls" hidden><label>IDX position<select id="idxPositions"></select></label><button id="seek" class="secondary">Jump to position</button><p id="indexInfo"></p></div><p>The file stays in this tab. Keep the browser open during LOAD; when the ESP buffer is full, the next chunk waits without keeping an HTTP request open.</p><a id="wifiLink" href="/wifi">Wi-Fi settings</a></section><section id="previewPanel" class="card" hidden><h2>Local preview</h2><p>Simulation without an ESP-01. Select a TAP or PRG and toggle MOTOR to check the animation, buffer and page layout.</p><button id="motorToggle" class="secondary">MOTOR on/off</button><button id="saveMock" class="secondary">Simulate SAVE</button></section></main>
<script>
const $ = id => document.getElementById(id);
const preview = location.protocol === 'file:' || new URLSearchParams(location.search).has('preview');
let source = null, busy = false, last = null;
let selectedFile = null, selectedIdx = null;
let selectedDefaultMachine = null;
let lastCounterValue = null, lastCounterRaw = null;
let counterOriginCycles = 0;
let mockBytes = 0, mockSize = 0, mockDemo = true, mockTick = Date.now();
let mockType = 'tap';
const mock = {tapeState:'loaded', sourceType:'tap', machine:'c64', clockHz:985248, fileName:'draxcaught (demo)', motorOn:false,
  sensePressed:true, autoPlay:true, defaultMachine:'c64', playing:false, phase:'idle', playedCycles:0,
  totalCycles:117000000, bufferedMillis:null, fileOffsetReceived:0,
  freeBytes:8192, sessionId:0, logicalSize:0, error:null, writeEnabled:false, gpioDisabled:false,
  freeHeap:32000, maxFreeBlock:24000, maxLoopUs:0, maxHttpUs:0,
  i2sUnderruns:0, sourceUnderruns:0, uploadErrors:0,
  writeStatus:'idle', writeMachine:'unknown', writeName:'', writeType:0, writeStart:0, writeEnd:0,
  writeHeaders:0, writeErrors:0, writeOverflows:0, scope:'', scopeHz:0};
let mockScopeLevel = 0;

function drawScope(s) {
  const canvas = $('scope');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.fillStyle = '#0c1117';
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = '#1e2a36';
  ctx.beginPath();
  ctx.moveTo(0, h / 2);
  ctx.lineTo(w, h / 2);
  ctx.stroke();
  const bits = typeof s.scope === 'string' ? s.scope : '';
  if (!bits.length) {
    $('scopeLabel').textContent = 'READ · waiting for playback';
    return;
  }
  const mid = h * 0.55, amp = h * 0.32;
  ctx.strokeStyle = '#7dcea0';
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (let i = 0; i < bits.length; i++) {
    const x = (i / Math.max(1, bits.length - 1)) * (w - 1);
    const y = bits[i] === '1' ? mid - amp : mid + amp;
    if (i === 0) ctx.moveTo(x, y);
    else {
      const prevY = bits[i - 1] === '1' ? mid - amp : mid + amp;
      ctx.lineTo(x, prevY);
      ctx.lineTo(x, y);
    }
  }
  ctx.stroke();
  const hz = s.scopeHz || 0;
  $('scopeLabel').textContent = hz ?
    'READ · ~' + Math.round(hz / 1000) + ' kSa/s · ' + bits.length + ' samples' :
    'READ · ' + bits.length + ' samples';
}

function motorBlocksEdit(s) {
  // REW/FF stay available; only tape insert/eject need MOTOR off on real GPIO.
  return !!s?.motorOn && !s?.gpioDisabled;
}

function updateMock() {
  const now = Date.now(), seconds = Math.min(1, (now - mockTick) / 1000);
  mockTick = now;
  if (mock.motorOn && mock.sensePressed && mock.tapeState === 'loaded' &&
      mock.phase !== 'error') {
    if (!mockDemo) mockBytes = Math.max(0, mockBytes - Math.ceil(seconds *
      (mockType === 'prg' ? 105 : 2200)));
    mock.playedCycles = Math.min(mock.totalCycles || Infinity,
      mock.playedCycles + Math.round(seconds * mock.clockHz));
    if (!mockDemo && mockBytes === 0 && mock.fileOffsetReceived < mockSize) {
      mock.error = 'buffer_underrun'; mock.phase = 'error';
    } else     if (mock.playedCycles >= mock.totalCycles) mock.phase = 'done';
    else mock.phase = 'data';
    let wave = mock.scope || '';
    const samples = Math.max(1, Math.min(24, Math.round(seconds * 80)));
    for (let i = 0; i < samples; i++) {
      if (Math.random() < 0.18) mockScopeLevel ^= 1;
      wave += mockScopeLevel ? '1' : '0';
    }
    mock.scope = wave.slice(-192);
    mock.scopeHz = 5000;
  } else if (mock.playedCycles && mock.phase !== 'error' && mock.phase !== 'done') {
    mock.phase = 'paused';
  }
  mock.playing = mock.motorOn && mock.sensePressed && mock.tapeState === 'loaded' &&
    mock.phase === 'data';
  mock.freeBytes = 8192 - mockBytes;
}

async function mockApi(path, options = {}) {
  updateMock();
  if (path === '/state') return {...mock};
  if (path === '/sense') {
    const pressed = options.body?.get('pressed');
    if (pressed !== '0' && pressed !== '1') throw new Error('Invalid PLAY state');
    mock.sensePressed = pressed === '1';
    return {sensePressed:mock.sensePressed};
  }
  if (path === '/autoplay') {
    if (mock.motorOn) throw new Error('Stop MOTOR');
    mock.autoPlay = options.body?.get('enabled') === '1';
    return {autoPlay:mock.autoPlay};
  }
  if (path === '/default-machine') {
    const machine = options.body?.get('machine');
    if (machine !== 'c64' && machine !== 'c16') throw new Error('Invalid computer');
    mock.defaultMachine = machine;
    return {defaultMachine:machine};
  }
  if (path === '/session') {
    const args = options.body;
    mockType = args.get('type') === 'prg' ? 'prg' : 'tap';
    const fileSize = Number(args.get('size'));
    mockSize = mockType === 'prg' ? 2 + 2 * (fileSize - 2) : fileSize;
    mock.logicalSize = mockSize;
    mockBytes = 0; mockDemo = false;
    mock.sessionId++; mock.fileOffsetReceived = mockType === 'prg' ? 2 : 20;
    mock.sourceType = mockType;
    mock.machine = mockType === 'prg' ? args.get('machine') :
      args.get('header')?.startsWith('433136') ? 'c16' : 'c64';
    mock.clockHz = mock.machine === 'c16' ? 886724 : 985248;
    mock.fileName = args.get('displayName') || args.get('name');
    mock.totalCycles = mockType === 'prg' ? prgTotalCycles(fileSize - 2, mock.machine) :
      Number(args.get('cycles'));
    mock.playedCycles = 0; mock.tapeState = 'ejected'; mock.phase = 'idle'; mock.error = null;
    mock.sensePressed = mock.autoPlay;
    return {sessionId:mock.sessionId, nextOffset:mock.fileOffsetReceived,
      logicalSize:mockSize, freeBytes:8192};
  }
  if (path.startsWith('/chunk?')) {
    const args = new URLSearchParams(path.split('?')[1]);
    const offset = Number(args.get('offset'));
    if (Number(args.get('sid')) !== mock.sessionId || offset !== mock.fileOffsetReceived)
      throw Object.assign(new Error('offset'), {data:{nextOffset:mock.fileOffsetReceived}});
    const size = options.body.get('data').size;
    if (size > mock.freeBytes) throw Object.assign(new Error('buffer'),
      {data:{nextOffset:mock.fileOffsetReceived}});
    mockBytes += size; mock.fileOffsetReceived += size;
    if (mockBytes >= 4096 || mock.fileOffsetReceived === mockSize) mock.tapeState = 'loaded';
    return {nextOffset:mock.fileOffsetReceived, freeBytes:8192-mockBytes, loaded:mock.tapeState==='loaded'};
  }
  if (path === '/seek') {
    const args = options.body;
    if (mockType !== 'tap' || mock.motorOn || Number(args.get('sid')) !== mock.sessionId)
      throw new Error('Stop MOTOR / stale session');
    const offset = Number(args.get('offset'));
    if (offset < 20 || offset >= mockSize) throw new Error('Invalid TAP offset');
    mock.sessionId++; mock.fileOffsetReceived = offset; mockBytes = 0;
    mock.tapeState = 'ejected'; mock.playedCycles = 0;
    mock.totalCycles = Number(args.get('cycles'));
    mock.phase = 'idle'; mock.error = null;
    return {sessionId:mock.sessionId, nextOffset:offset, freeBytes:8192, tapeState:'ejected'};
  }
  if (path === '/eject') {
    if (mock.motorOn) throw new Error('Stop MOTOR');
    mock.tapeState = 'ejected'; mockBytes = 0; mock.phase = 'idle';
    mock.totalCycles = 0; mock.playedCycles = 0; mock.logicalSize = 0;
    mock.sessionId = 0; mock.fileOffsetReceived = 0; mock.sensePressed = false; source = null;
    return {tapeState:'ejected'};
  }
  if (path === '/nav') {
    const dir = Number(options.body?.get('dir'));
    if (dir !== 1 && dir !== -1) throw new Error('dir');
    const counter = tapeCounterAtSeconds(mock.playedCycles / mock.clockHz);
    const target = dir > 0 ? Math.floor(counter / 10) * 10 + 10 :
      (counter % 10 === 0 ? Math.max(0, counter - 10) : Math.floor(counter / 10) * 10);
    mock.playedCycles = secondsAtTapeCounter(target) * mock.clockHz;
    mock.scope = '';
    return {offset:20, playedCycles:mock.playedCycles, tapeState:mock.tapeState};
  }
  if (path === '/demo') {
    if (mock.motorOn) throw new Error('Stop MOTOR');
    mock.tapeState = 'loaded'; mock.fileName = 'draxcaught (demo)';
    mockDemo = true; mockType = 'tap'; mock.sourceType = 'tap';
    mock.machine = 'c64'; mock.clockHz = 985248;
    mock.sessionId = 0; mock.logicalSize = 0; mock.fileOffsetReceived = 0;
    mock.playedCycles = 0; mock.sensePressed = mock.autoPlay; mock.scope = '';
    mock.totalCycles = 117000000; mock.phase = 'idle'; mock.error = null;
    return {tapeState:'loaded'};
  }
  throw new Error('Unknown request: ' + path);
}

async function api(path, options) {
  if (preview) return mockApi(path, options);
  const response = await fetch(path, options);
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw Object.assign(new Error(data.error || response.statusText), {data});
  return data;
}

function prgTotalCycles(length, machine = 'c64') {
  const [short, medium, long] = machine === 'c16' ? [448, 896, 1792] : [384, 536, 680];
  const byteCycles = long + medium + 9 * (short + medium);
  return (20000 + 79 + 5000 + 79) * short +
    (2 * (9 + 192 + 1) + 2 * (9 + length + 1)) * byteCycles +
    4 * (long + (machine === 'c16' ? medium : short)) + 200000;
}

function time(cycles, clockHz = 985248) {
  if (cycles == null) return '—';
  const seconds = Math.round(cycles / clockHz);
  return Math.floor(seconds / 60) + ':' + String(seconds % 60).padStart(2, '0');
}

// Same spool geometry and rounding as Pi1551's TapeCounterAtTimeMs(),
// with VICE's tape thickness, hub radius, tape speed and counter gearing.
function tapeCounterAtSeconds(seconds) {
  const d = 1.27e-5, r = 1.07e-2, v = 4.76e-2, g = 0.525;
  const t = Math.floor(Math.max(0, seconds) * 1000) / 1000;
  return Math.floor(g * (Math.sqrt(v * t / (d * Math.PI) + (r / d) ** 2) - r / d) + 0.5);
}

function secondsAtTapeCounter(counter) {
  const d = 1.27e-5, r = 1.07e-2, v = 4.76e-2, g = 0.525;
  const turns = counter / g + r / d;
  return (turns * turns - (r / d) ** 2) * d * Math.PI / v;
}

function showTapeCounter(raw) {
  const value = raw % 1000;
  const digits = String(value).padStart(3, '0');
  const before = String(lastCounterValue ?? value).padStart(3, '0');
  const animate = lastCounterRaw !== null && raw === lastCounterRaw + 1;
  for (let i = 0; i < 3; i++) {
    if (lastCounterValue === value) break;
    const strip = $('counterStrip' + i);
    strip.classList.remove('rolling');
    $('counterOld' + i).textContent = animate ? before[i] : digits[i];
    $('counterNext' + i).textContent = digits[i];
    if (animate && before[i] !== digits[i]) {
      void strip.offsetWidth; // Restart the upward roll for this digit.
      strip.classList.add('rolling');
    }
  }
  $('counter').setAttribute('aria-label', 'Tape counter ' + digits);
  lastCounterValue = value;
  lastCounterRaw = raw;
}

function render(s) {
  last = s;
  const playing = !!s.playing;
  const editBlocked = motorBlocksEdit(s);
  const canServerNav = s.tapeState === 'loaded' && s.sourceType === 'tap';
  $('deck').classList.toggle('playing', playing);
  $('deck').classList.toggle('ejected', s.tapeState === 'ejected');
  $('play').classList.toggle('pressed', !!s.sensePressed);
  $('stop').classList.toggle('pressed', !s.sensePressed);
  $('play').setAttribute('aria-pressed', String(!!s.sensePressed));
  $('stop').setAttribute('aria-pressed', String(!s.sensePressed));
  // REW/FF stay enabled whenever a TAP (or demo) can be navigated.
  $('rewind').disabled = !source &&
    (s.tapeState !== 'loaded' || s.sessionId !== 0) && !canServerNav;
  $('fastForward').disabled = !(navigationTarget(1) || canServerNav);
  $('deckEject').disabled = editBlocked || s.tapeState === 'ejected' && !s.sessionId;
  if (!$('deckMessage').textContent)
    $('deckMessage').textContent = s.sensePressed ?
      (s.motorOn ?
        'PLAY + MOTOR: tape running.' :
        'PLAY pressed; waiting for MOTOR.') :
      'STOP released PLAY. Tape paused until PLAY + MOTOR.';
  $('loaded').textContent = s.tapeState === 'loaded' ? (s.fileName || 'Loaded') : 'Ejected';
  $('motor').textContent = s.motorOn ?
    (s.gpioDisabled ? 'On (soft)' : 'On') : 'Off';
  $('autoPlay').checked = !!s.autoPlay;
  if (s.defaultMachine === 'c64' || s.defaultMachine === 'c16') {
    $('defaultMachine').value = s.defaultMachine;
    if (selectedDefaultMachine !== s.defaultMachine) {
      selectedDefaultMachine = s.defaultMachine;
      $('machine').value = s.defaultMachine;
    }
  }
  const origin = source?.type === 'tap' && source.id === s.sessionId ?
    source.seekCycles || 0 : s.phase === 'done' || s.phase === 'error' ? counterOriginCycles : 0;
  const absoluteCycles = origin + (s.playedCycles || 0);
  showTapeCounter(tapeCounterAtSeconds(absoluteCycles / s.clockHz));
  const total = s.totalCycles || (source && source.id === s.sessionId ? source.total : null);
  $('played').textContent = time(s.playedCycles, s.clockHz) +
    (total ? ' / ' + time(total, s.clockHz) : '');
  $('remaining').textContent = total ? time(Math.max(0, total - s.playedCycles), s.clockHz) : '—';
  $('buffer').textContent = s.embedded ?
    ('ROM · ' + (s.logicalSize || 0) + ' B') :
    (s.fileOffsetReceived ? (8192 - s.freeBytes) + ' B' : '—');
  $('phase').textContent = 'Phase: ' + s.phase + ' · received ' + s.fileOffsetReceived + ' B';
  if ($('debugPanel').open) $('debugJson').textContent = JSON.stringify(s, null, 2);
  $('diagnostics').textContent = s.freeHeap == null ? '' :
    'ESP: heap ' + s.freeHeap + ' B (largest block ' + s.maxFreeBlock + ' B)' +
    ' · max loop/HTTP ' + s.maxLoopUs + '/' + s.maxHttpUs + ' µs' +
    ' · I2S/source underruns ' + s.i2sUnderruns + '/' + s.sourceUnderruns +
    ' · upload errors ' + s.uploadErrors;
  const writeLabels = {idle:'Waiting', listening:'Listening', receiving_header:'Receiving header',
    header_verified_no_action:'Header verified', incomplete:'Incomplete header',
    capture_overflow:'Capture overflow', checksum_error:'Checksum error',
    copy_mismatch:'Copies differ', parity_error:'Parity error',
    sync_error:'Sync error', pulse_error:'Pulse error',
    invalid_header:'Invalid header',
    missing_terminator:'Missing block terminator', unexpected_terminator:'Unexpected block terminator'};
  $('writeStatus').textContent = s.writeEnabled ?
    (writeLabels[s.writeStatus] || s.writeStatus) : 'Disabled';
  $('writeDetail').textContent = s.writeStatus === 'header_verified_no_action' ?
    (s.writeMachine === 'c16' ? 'C16/Plus4' : 'C64') + ' · SAVE "' + s.writeName + '" · type ' + s.writeType + ' · $' +
    s.writeStart.toString(16).toUpperCase().padStart(4, '0') + '–$' +
    s.writeEnd.toString(16).toUpperCase().padStart(4, '0') +
    ' · no action assigned' :
    (s.writeEnabled ? 'Verified headers: ' + s.writeHeaders +
      ' · errors: ' + s.writeErrors + ' · overflows: ' + s.writeOverflows :
      'GPIO2/WRITE requires protected hardware and a WRITE profile.');
  if (s.error) $('error').textContent = 'Player error: ' + s.error;
  $('seek').disabled = !source || source.type !== 'tap' || source.id !== s.sessionId;
  drawScope(s);
}

async function parseIdx(file) {
  if (!file) return [];
  if (file.size > 262144) throw Error('IDX is too large');
  const lines = (await file.text()).replace(/^\uFEFF/, '').split(/\r?\n/);
  const entries = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line || /^[#;']/.test(line)) continue;
    const match = /^(0[xX][0-9a-fA-F]+|0[0-7]+|[1-9][0-9]*|0)[ \t]+(.+)$/.exec(line);
    if (!match) throw Error('Unknown IDX format on line ' + (i + 1));
    const token = match[1];
    const radix = /^0[xX]/.test(token) ? 16 : /^0[0-7]+$/.test(token) ? 8 : 10;
    const offset = parseInt(token, radix);
    if (!Number.isSafeInteger(offset) || offset < 20 || offset > 0xffffffff ||
        !match[2].trim())
      throw Error('Invalid IDX offset or label on line ' + (i + 1));
    entries.push({offset, label:match[2].trim().slice(0, 120)});
    if (entries.length > 512) throw Error('IDX has too many entries');
  }
  if (!entries.length) throw Error('IDX contains no entries');
  return entries;
}

function tapFormat(bytes, size) {
  if (bytes.length !== 20 || size <= 20) throw Error('Incomplete TAP header');
  const signature = String.fromCharCode(...bytes.slice(0, 12));
  const c64 = signature === 'C64-TAPE-RAW' && bytes[13] === 0 &&
    (bytes[12] === 1 || bytes[12] === 2);
  const c16 = signature === 'C16-TAPE-RAW' && bytes[13] === 2 &&
    (bytes[12] === 1 || bytes[12] === 2);
  if (bytes[14] !== 0 || !(c64 || c16))
    throw Error('Supported: C64 and C16 PAL TAP v1/v2');
  const declared = new DataView(bytes.buffer, bytes.byteOffset, 20).getUint32(16, true);
  if (declared !== size - 20) throw Error('TAP data length does not match the header');
  return {machine:c16 ? 'c16' : 'c64', version:bytes[12]};
}

async function scanTape(file, indexedOffsets = [], clockHz = 985248) {
  const wanted = new Set(indexedOffsets), positions = new Map(), pulsePositions = new Map();
  const timeline = [{offset:20, prefixCycles:0, phase:0}];
  let position = 20, total = 0, pulses = 0, nextMark = clockHz * 10;
  while (position < file.size) {
    const bytes = new Uint8Array(await file.slice(position,
      Math.min(file.size, position + 65539)).arrayBuffer());
    const limit = Math.min(65536, file.size - position);
    let i = 0;
    while (i < limit) {
      const absolute = position + i;
      if (total >= nextMark) {
        timeline.push({offset:absolute, prefixCycles:total, phase:pulses % 2});
        while (total >= nextMark) nextMark += clockHz * 10;
      }
      if (wanted.has(absolute)) {
        positions.set(absolute, total);
        pulsePositions.set(absolute, pulses);
      }
      if (bytes[i]) { total += bytes[i] * 8; i++; }
      else {
        if (i + 3 >= bytes.length) throw Error('Truncated TAP pulse');
        const cycles = bytes[i+1] | (bytes[i+2] << 8) | (bytes[i+3] << 16);
        if (!cycles) throw Error('Zero-length TAP pulse');
        total += cycles; i += 4;
      }
      pulses++;
    }
    position += i;
  }
  return {total, positions, pulsePositions, timeline};
}

function showIndex(entries, warning = '') {
  const select = $('idxPositions');
  select.replaceChildren();
  for (const entry of entries) {
    const option = document.createElement('option');
    option.value = String(entry.offset);
    option.textContent = '0x' + entry.offset.toString(16).toUpperCase() + ' · ' + entry.label;
    select.append(option);
  }
  $('indexControls').hidden = entries.length === 0;
  $('indexInfo').textContent = warning || entries.length + ' valid positions';
}

function selectedFilesText() {
  $('selectedFiles').textContent = (selectedFile?.name || 'No file') +
    (selectedIdx ? ' + ' + selectedIdx.name : '');
}
function updateFormatControls() {
  const isPrg = /\.prg$/i.test(selectedFile?.name || '');
  $('prgOptions').hidden = !isPrg;
  $('indexPicker').hidden = isPrg;
  if (isPrg) $('programName').value = selectedFile.name.replace(/\.prg$/i, '').slice(0, 16);
}

function sourceChunk(item, offset, limit) {
  let length = Math.min(1024, limit, item.logicalSize - offset);
  if (item.type === 'prg') {
    const withinCopy = (offset - 2) % item.dataLength;
    length = Math.min(length, item.dataLength - withinCopy);
    const start = 2 + withinCopy;
    return item.file.slice(start, start + length);
  }
  return item.file.slice(offset, offset + length);
}

function currentTapeCycles() {
  return (source?.seekCycles || 0) +
    (source && last?.sessionId === source.id ? last.playedCycles || 0 : 0);
}

function navigationTarget(direction) {
  if (!source || source.type !== 'tap') return null;
  const current = currentTapeCycles();
  const epsilon = Math.max(1, (last?.clockHz || 985248) / 100);
  if (source.indexEntries.length) {
    const start = {offset:20, prefixCycles:0, phase:0};
    const end = {offset:source.logicalSize, prefixCycles:source.fullTotal, phase:0, atEnd:true};
    const entries = [start, ...source.indexEntries];
    if (direction > 0) {
      const next = entries.find(entry => entry.prefixCycles > current + epsilon);
      return next || end;
    }
    const previous = entries.filter(entry => entry.prefixCycles < current - epsilon);
    return previous.length ? previous[previous.length - 1] : start;
  }
  const clockHz = last?.clockHz || 985248;
  const currentCounter = tapeCounterAtSeconds(current / clockHz);
  const targetCounter = Math.max(0, Math.floor(currentCounter / 10) * 10 +
    (direction > 0 ? 10 : currentCounter % 10 === 0 ? -10 : 0));
  const target = secondsAtTapeCounter(targetCounter) * clockHz;
  if (direction > 0) {
    const next = source.timeline.find(entry => entry.prefixCycles >= target) ||
      [...source.timeline].reverse().find(entry => entry.prefixCycles > current);
    return next || {offset:source.logicalSize, prefixCycles:source.fullTotal, phase:0, atEnd:true};
  }
  const previous = source.timeline.filter(entry => entry.prefixCycles <= target);
  return previous[previous.length - 1] || source.timeline[0] ||
    {offset:20, prefixCycles:0, phase:0};
}

async function acquire() {
  while (busy) await new Promise(resolve => setTimeout(resolve, 25));
  busy = true;
}

function reconcileSource(item, state) {
  if (!item) return {ok:false, reason:'no_file'};
  if (state.sessionId !== item.id || state.sourceType !== item.type ||
      state.logicalSize !== item.logicalSize)
    return {ok:false, reason:'session_changed'};
  if (state.phase === 'error') return {ok:false, reason:'playback_error'};
  const offset = state.fileOffsetReceived;
  if (!Number.isSafeInteger(offset) || offset < (item.type === 'prg' ? 2 : 20) ||
      offset > item.logicalSize)
    return {ok:false, reason:'invalid_offset'};
  const changed = item.offset !== offset;
  item.offset = offset; // ESP is authoritative after an uncertain HTTP result.
  return {ok:true, changed};
}

async function step() {
  if (busy) return;
  busy = true;
  try {
    const state = await api('/state');
    const hadSource = !!source;
    const recovery = reconcileSource(source, state);
    if (hadSource && !recovery.ok) {
      source = null;
      showIndex([]);
    }
    if ($('error').textContent.startsWith('Connection/transfer:'))
      $('error').textContent = '';
    render(state);
    $('transport').textContent = hadSource && recovery.ok ?
      (recovery.changed ? 'Connection restored; offset reconciled with ESP.' : 'Connected to ESP.') :
      state.sessionId ? (recovery.reason === 'playback_error' ?
        'Playback stopped; insert the file again.' :
        'This tab has no source file for the session. Insert the file again.') : 'Connected to ESP.';
    if (source && recovery.ok) {
      let credit = state.freeBytes;
      for (let requests = 0; requests < 4 && credit > 0 &&
           source.offset < source.logicalSize; requests++) {
        const blob = sourceChunk(source, source.offset, credit);
        const form = new FormData();
        form.append('data', blob, 'chunk.bin');
        try {
          const ack = await api('/chunk?sid=' + source.id + '&offset=' + source.offset,
            {method:'POST', body:form});
          source.offset = ack.nextOffset;
          credit = ack.freeBytes;
        } catch (error) {
          // A 409 may mean stale offset, stale session, or a full ring. The
          // next GET /state decides which; never trust this reply alone.
          if (error.data && Number.isInteger(error.data.nextOffset)) break;
          throw error;
        }
      }
    }
  } catch (error) {
    $('transport').textContent = 'Connection lost; the file remains in this tab.';
    $('error').textContent = 'Connection/transfer: ' + error.message;
  } finally { busy = false; }
}

$('load').onclick = async () => {
  const file = selectedFile || $('file').files[0];
  if (!file) { $('error').textContent = 'Select a TAP or PRG'; return; }
  if (motorBlocksEdit(last)) { $('error').textContent = 'Stop MOTOR'; return; }
  await acquire();
  try {
    const isPrg = /\.prg$/i.test(file.name);
    if (!isPrg && !/\.tap$/i.test(file.name)) throw Error('Select a TAP or PRG');
    const bytes = new Uint8Array(await file.slice(0, isPrg ? 2 : 20).arrayBuffer());
    const header = Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
    let body, scan = null, validEntries = [], indexEntries = [];
    if (isPrg) {
      if (file.size < 3) throw Error('PRG contains no program data');
      body = new URLSearchParams({type:'prg', size:String(file.size), header,
        name:file.name, displayName:$('programName').value || file.name.replace(/\.prg$/i, ''),
        machine:$('machine').value, turbo:$('turbo').value});
    } else {
      const format = tapFormat(bytes, file.size);
      indexEntries = await parseIdx(selectedIdx);
      scan = await scanTape(file, indexEntries.map(entry => entry.offset),
        format.machine === 'c16' ? 886724 : 985248);
      validEntries = indexEntries.filter(entry =>
        entry.offset >= 20 && entry.offset < file.size && scan.positions.has(entry.offset))
        .map(entry => ({...entry, prefixCycles:scan.positions.get(entry.offset),
          phase:format.version === 2 ? scan.pulsePositions.get(entry.offset) % 2 : 0}))
        .sort((a, b) => a.offset - b.offset);
      if (indexEntries.length && !validEntries.length)
        throw Error('IDX has no entry at a TAP record boundary');
      body = new URLSearchParams({type:'tap', size:String(file.size), header,
        name:file.name, cycles:String(scan.total)});
    }
    const ack = await api('/session', {method:'POST', body});
    counterOriginCycles = 0;
    source = {file, type:isPrg ? 'prg' : 'tap', id:ack.sessionId,
      offset:ack.nextOffset, logicalSize:ack.logicalSize || file.size,
      dataLength:isPrg ? file.size - 2 : 0,
      total:isPrg ? null : scan.total, fullTotal:isPrg ? null : scan.total,
      indexEntries:validEntries, timeline:isPrg ? [] : scan.timeline.map(entry =>
        ({...entry, phase:bytes[12] === 2 ? entry.phase : 0})),
      seekOffset:20, seekCycles:0,
      sessionBody:isPrg ? new URLSearchParams(body) : null};
    if (isPrg) showIndex([]);
    else {
      const mismatch = selectedIdx && selectedIdx.name.replace(/\.idx$/i,'').toLowerCase() !==
        file.name.replace(/\.tap$/i,'').toLowerCase();
      showIndex(validEntries, (indexEntries.length - validEntries.length) +
        ' entries skipped (outside TAP or inside a record)' +
        (mismatch ? ' · TAP/IDX names differ' : ''));
    }
    $('error').textContent = '';
  } catch (error) {
    $('error').textContent = 'Cannot insert file: ' + error.message;
  } finally { busy = false; }
};

$('deckEject').onclick = async () => {
  await acquire();
  try {
    await api('/eject', {method:'POST'});
    counterOriginCycles = 0;
    source = null; showIndex([]);
    $('error').textContent = '';
    $('deckMessage').textContent = 'Tape ejected.';
  }
  catch (error) { $('error').textContent = error.message; }
  finally { busy = false; }
};
$('demo').onclick = async () => {
  $('deck').scrollIntoView({behavior: 'smooth', block: 'start'});
  await acquire();
  try {
    await api('/demo', {method:'POST'});
    counterOriginCycles = 0; source = null; showIndex([]);
    $('error').textContent = '';
    $('deckMessage').textContent = 'Loading embedded demo…';
    // Demo insert runs in the ESP loop (not inside HTTP) to avoid WDT/Wi-Fi drops.
    for (let i = 0; i < 300; i++) {
      await new Promise((r) => setTimeout(r, 100));
      const s = await api('/state');
      render(s);
      if (s.tapeState === 'loaded') {
        $('deckMessage').textContent = 'Demo tape loaded.';
        $('deck').scrollIntoView({behavior: 'smooth', block: 'start'});
        break;
      }
      if (s.tapeState === 'error') {
        throw new Error(s.error || 'demo failed');
      }
    }
  } catch (error) { $('error').textContent = error.message; }
  finally { busy = false; }
};

async function serverNav(dir) {
  await acquire();
  try {
    await api('/nav', {method:'POST', body:new URLSearchParams({dir:String(dir)})});
    counterOriginCycles = 0;
    $('error').textContent = '';
    $('deckMessage').textContent = dir > 0 ? 'Fast-forwarded.' : 'Rewound.';
  } catch (error) {
    $('error').textContent = 'Cannot seek: ' + error.message;
  } finally { busy = false; }
}

async function seekTo(entry) {
  if (!source) {
    $('error').textContent = 'Select a TAP';
    return;
  }
  await acquire();
  try {
    if (entry.atEnd) {
      const body = new URLSearchParams({sid:String(source.id),
        offset:String(source.logicalSize), cycles:String(source.fullTotal),
        phase:'0', end:'1'});
      const ack = await api('/seek', {method:'POST', body});
      source.id = ack.sessionId;
      source.offset = ack.nextOffset;
      source.total = 0;
      source.seekOffset = source.logicalSize;
      source.seekCycles = 0;
      counterOriginCycles = 0;
      $('error').textContent = '';
      $('deckMessage').textContent = 'Fast-forwarded to end of tape.';
      return;
    }
    const remaining = source.fullTotal - entry.prefixCycles;
    const body = new URLSearchParams({sid:String(source.id),
      offset:String(entry.offset), cycles:String(remaining), phase:String(entry.phase)});
    const ack = await api('/seek', {method:'POST', body});
    source.id = ack.sessionId;
    source.offset = ack.nextOffset;
    source.total = remaining;
    source.seekOffset = entry.offset;
    source.seekCycles = entry.prefixCycles;
    counterOriginCycles = entry.prefixCycles;
    if (source.indexEntries.some(item => item.offset === entry.offset))
      $('idxPositions').value = String(entry.offset);
    $('error').textContent = '';
    $('deckMessage').textContent = entry.offset === 20 ?
      'Rewound to start.' : 'Jumped to 0x' + entry.offset.toString(16).toUpperCase() + '.';
  } catch (error) {
    $('error').textContent = 'Cannot seek: ' + error.message;
  } finally { busy = false; }
}

$('seek').onclick = async () => {
  const offset = Number($('idxPositions').value);
  const entry = source?.indexEntries.find(item => item.offset === offset);
  if (!entry) { $('error').textContent = 'Invalid IDX position'; return; }
  await seekTo(entry);
};

$('rewind').onclick = async () => {
  if (!source && last?.tapeState === 'loaded' && last.sessionId === 0) {
    await acquire();
    try {
      await api('/demo', {method:'POST'});
      counterOriginCycles = 0;
      $('deckMessage').textContent = 'Demo tape rewound to start.';
      $('error').textContent = '';
    } catch (error) {
      $('error').textContent = 'Cannot rewind demo: ' + error.message;
    } finally { busy = false; }
    return;
  }
  if (source?.type === 'tap') {
    const prev = navigationTarget(-1);
    if (prev) return seekTo(prev);
  }
  if (!source && last?.tapeState === 'loaded' && last.sourceType === 'tap')
    return serverNav(-1);
  if (!source) return;
  await acquire();
  try {
    const ack = await api('/session', {method:'POST', body:source.sessionBody});
    source.id = ack.sessionId;
    source.offset = ack.nextOffset;
    source.seekOffset = 2;
    counterOriginCycles = 0;
    $('error').textContent = '';
    $('deckMessage').textContent = 'Program rewound to start.';
  } catch (error) {
    $('error').textContent = 'Cannot rewind: ' + error.message;
  } finally { busy = false; }
};

$('fastForward').onclick = async () => {
  if (source?.type === 'tap') {
    const next = navigationTarget(1);
    if (next) return seekTo(next);
  }
  if (last?.tapeState === 'loaded' && last.sourceType === 'tap')
    return serverNav(1);
};

async function setSense(pressed) {
  await acquire();
  try {
    const ack = await api('/sense', {method:'POST',
      body:new URLSearchParams({pressed:pressed ? '1' : '0'})});
    if (last) render({...last, sensePressed:ack.sensePressed});
    $('deckMessage').textContent = pressed ?
      'PLAY pressed. Tape runs when MOTOR is also on.' :
      'STOP released PLAY. Tape paused.';
    $('error').textContent = '';
  } catch (error) {
    $('error').textContent = 'Cannot change PLAY: ' + error.message;
  } finally { busy = false; }
}
$('play').onclick = () => setSense(true);
$('stop').onclick = () => setSense(false);

$('autoPlay').onchange = async () => {
  const enabled = $('autoPlay').checked;
  await acquire();
  try {
    const ack = await api('/autoplay', {method:'POST',
      body:new URLSearchParams({enabled:enabled ? '1' : '0'})});
    if (last) render({...last, autoPlay:ack.autoPlay});
    $('deckMessage').textContent = ack.autoPlay ?
      'Autoplay: inserting the next tape will press PLAY.' :
      'Autoplay off: press PLAY after inserting a tape.';
    $('error').textContent = '';
  } catch (error) {
    $('autoPlay').checked = !enabled;
    $('error').textContent = 'Cannot save autoplay: ' + error.message;
  } finally { busy = false; }
};

$('defaultMachine').onchange = async () => {
  const machine = $('defaultMachine').value;
  await acquire();
  try {
    const ack = await api('/default-machine', {method:'POST',
      body:new URLSearchParams({machine})});
    if (last) render({...last, defaultMachine:ack.defaultMachine});
    $('deckMessage').textContent = 'Default PRG computer: ' +
      (ack.defaultMachine === 'c16' ? 'C16 / Plus/4' : 'C64') + '.';
    $('error').textContent = '';
  } catch (error) {
    $('defaultMachine').value = selectedDefaultMachine || 'c64';
    $('error').textContent = 'Cannot save default computer: ' + error.message;
  } finally { busy = false; }
};

$('file').onchange = event => { selectedFile = event.target.files[0] || null; selectedFilesText(); updateFormatControls(); $('machine').value = selectedDefaultMachine || 'c64'; };
$('indexFile').onchange = event => { selectedIdx = event.target.files[0] || null; selectedFilesText(); };
$('clearIndex').onclick = () => { selectedIdx = null; $('indexFile').value = ''; selectedFilesText(); };
const dropzone = $('dropzone');
dropzone.ondragover = event => { event.preventDefault(); dropzone.classList.add('over'); };
dropzone.ondragleave = () => dropzone.classList.remove('over');
dropzone.ondrop = event => {
  event.preventDefault(); dropzone.classList.remove('over');
  for (const file of event.dataTransfer.files) {
    if (/\.(tap|prg)$/i.test(file.name)) selectedFile = file;
    else if (/\.idx$/i.test(file.name)) selectedIdx = file;
  }
  selectedFilesText(); updateFormatControls();
  $('machine').value = selectedDefaultMachine || 'c64';
};
selectedFilesText(); updateFormatControls();

if (preview) {
  $('previewPanel').hidden = false;
  $('wifiLink').hidden = true;
  mock.gpioDisabled = true;
  mock.motorOn = true;
  $('motorToggle').onclick = () => { mock.motorOn = !mock.motorOn; updateMock(); render({...mock}); };
  $('saveMock').onclick = () => { mock.writeEnabled = true;
    mock.writeStatus = 'header_verified_no_action'; mock.writeMachine = 'c64';
    mock.writeName = 'ESP:TEST';
    mock.writeType = 1; mock.writeStart = 0x0801; mock.writeEnd = 0x0810;
    mock.writeHeaders++; render({...mock}); };
}
async function loop() {
  await step();
  setTimeout(loop, source && source.offset < source.logicalSize ?
    (last?.playing ? 80 : 250) : 350);
}
loop();
</script></html>
)PAGE";
