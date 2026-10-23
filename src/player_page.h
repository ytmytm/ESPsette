#pragma once

#include <pgmspace.h>

// Generated from web/player.html by scripts/embed-player.py. Do not edit here.
static const char PLAYER_PAGE[] PROGMEM = R"PAGE(<!doctype html>
<html lang="pl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>espsette</title>
<style>
:root{color-scheme:dark;font:16px system-ui}*{box-sizing:border-box}body{margin:0;background:#11151b;color:#edf0f2}
main{max-width:760px;margin:auto;padding:18px}h1{font-size:1.6rem;margin:0 0 4px}p{line-height:1.45;color:#abb7c3}
.deck{background:linear-gradient(135deg,#373d44,#1d232b 58%,#353d42);border:3px solid #60666b;border-radius:20px;padding:20px;box-shadow:0 22px 45px #0008,inset 0 2px 4px #ffffff33}
.window{height:205px;border:12px solid #141a20;border-radius:10px;background:linear-gradient(145deg,#090c10,#20252c);box-shadow:inset 0 5px 15px #000;display:flex;align-items:center;justify-content:center;overflow:hidden}
.cassette{width:min(93%,440px);height:150px;border-radius:12px;background:linear-gradient(160deg,#afaaa0,#e0d9ca 40%,#aea89b);border:5px solid #3b3730;box-shadow:0 5px 12px #0008;position:relative;display:flex;align-items:center;justify-content:space-around}
.cassette:before{content:'espsette';position:absolute;top:8px;left:35%;font-weight:700;letter-spacing:.15em;color:#3c3b38}
.reel{width:72px;height:72px;border-radius:50%;border:8px solid #d5d2c6;background:repeating-conic-gradient(#373737 0 18deg,#e9e8e2 18deg 31deg,#777 31deg 60deg);box-shadow:0 0 0 15px #715e4d,inset 0 0 0 9px #c9c6bb}
.playing .reel{animation:spin 1.6s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}
.has-video .cassette,.ejected .cassette{display:none}.window video{width:100%;height:100%;object-fit:cover}.window video:not([src]),.ejected .window video{display:none}.ejected .window:after{content:'PUSTY DECK';color:#56616e;letter-spacing:.2em}
.keys{display:flex;gap:7px;justify-content:center;margin-top:15px}.key{background:linear-gradient(#24272b,#0c1015);border:2px solid #535a62;border-radius:4px 4px 10px 10px;padding:9px 14px;box-shadow:0 5px 2px #080a0d;color:#b9c4cb}.key.pressed{transform:translateY(4px);box-shadow:0 1px 2px #080a0d;color:#fff}
.card{background:#202832;border:1px solid #36414b;border-radius:12px;padding:16px;margin-top:18px}
label{display:block;margin:12px 0}input[type=file],select{display:block;width:100%;margin-top:8px}select{font:inherit;padding:10px;background:#111923;color:#fff;border:1px solid #566472;border-radius:8px}.drop{border:2px dashed #687786;border-radius:10px;padding:18px;text-align:center;color:#b9c7d3}.drop.over{border-color:#e2b94d;background:#39414a}button{font:inherit;border:0;border-radius:8px;background:#d5a43a;color:#17191a;padding:10px 15px;margin:4px 5px 4px 0;font-weight:700}button.secondary{background:#414e5b;color:#fff}button:disabled{opacity:.45}a{color:#9fcfff}
.stats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.stats div{background:#131a22;border-radius:7px;padding:12px}.stats b{display:block;font-size:1.2rem;overflow-wrap:anywhere}small{color:#a4b3bf}#error{color:#ffaaa6;min-height:1.3em}
</style>
<main><h1>espsette</h1><p>Kaseta przez Wi-Fi · C64 / C16 PAL</p>
<div id="deck" class="deck"><div class="window"><video id="windowVideo" muted loop playsinline></video><div class="cassette"><div class="reel"></div><div class="reel"></div></div></div><div class="keys"><span id="play" class="key pressed">PLAY</span><span class="key">STOP</span><span class="key">REW</span></div></div>
<section class="card"><div class="stats"><div><small>Kaseta</small><b id="loaded">—</b></div><div><small>Motor</small><b id="motor">—</b></div><div><small>Odtworzone</small><b id="played">—</b></div><div><small>Bufor ESP</small><b id="buffer">—</b></div><div><small>Pozostało sygnału</small><b id="remaining">—</b></div><div><small>WRITE / SAVE</small><b id="writeStatus">—</b></div></div><p id="writeDetail"></p><p id="phase">Łączenie…</p><p id="error"></p></section>
<section class="card"><div id="dropzone" class="drop">Upuść TAP lub PRG; opcjonalnie także IDX dla TAP</div><p id="selectedFiles"></p><label>Kaseta TAP C64 PAL v1 lub program PRG<input id="file" type="file" accept=".tap,.prg"></label><div id="indexPicker"><label>Indeks IDX (opcjonalny dla TAP)<input id="indexFile" type="file" accept=".idx,text/plain"></label><button id="clearIndex" class="secondary">Usuń wybór IDX</button></div><div id="prgOptions" hidden><label>Komputer<select id="machine"><option value="c64">C64</option><option value="c16">C16 / Plus/4 PAL</option></select></label><label>Dołącz loader turbo<select id="turbo"><option value="0">Nie — standardowy LOAD</option><option value="1" disabled>Tak — w przygotowaniu</option></select></label><label>Nazwa na kasecie<input id="programName" maxlength="16" placeholder="PROGRAM"></label></div><button id="load">Włóż kasetę</button><button id="demo" class="secondary">Kaseta demo</button><button id="eject" class="secondary">Wysuń</button><div id="indexControls" hidden><label>Pozycja z IDX<select id="idxPositions"></select></label><button id="seek" class="secondary">Przeskocz do pozycji</button><p id="indexInfo"></p></div><p>Plik pozostaje w tej karcie. Zostaw przeglądarkę otwartą podczas LOAD; przy braku miejsca kolejna porcja zaczeka bez otwartego żądania HTTP.</p><label>Opcjonalny film w okienku<input id="previewVideoFile" type="file" accept="video/mp4,video/webm"></label><p>Możesz użyć lokalnie pobranego <a href="https://pixabay.com/videos/tape-recorder-music-stale-nostalgic-118790/" target="_blank" rel="noopener">filmu magnetofonu z Pixabay</a>. Film pozostaje w przeglądarce i zatrzymuje się razem z MOTOR.</p><a id="wifiLink" href="/wifi">Konfiguracja Wi-Fi</a></section><section id="previewPanel" class="card" hidden><h2>Podgląd lokalny</h2><p>Symulacja bez ESP-01. Wybierz TAP lub PRG i przełącz MOTOR, aby sprawdzić animację, bufor i układ strony.</p><button id="motorToggle" class="secondary">MOTOR on/off</button><button id="saveMock" class="secondary">Symuluj SAVE</button></section></main>
<script>
const $ = id => document.getElementById(id);
const preview = location.protocol === 'file:' || new URLSearchParams(location.search).has('preview');
let source = null, busy = false, last = null, videoUrl = null;
let selectedFile = null, selectedIdx = null;
let mockBytes = 0, mockSize = 0, mockDemo = true, mockTick = Date.now();
let mockType = 'tap';
const mock = {tapeState:'loaded', sourceType:'tap', machine:'c64', clockHz:985248, fileName:'draxcaught (demo)', motorOn:false,
  sensePressed:true, playing:false, phase:'idle', playedCycles:0,
  totalCycles:117000000, bufferedMillis:null, fileOffsetReceived:0,
  freeBytes:8192, sessionId:0, error:null, writeEnabled:false,
  writeStatus:'idle', writeMachine:'unknown', writeName:'', writeType:0, writeStart:0, writeEnd:0,
  writeHeaders:0, writeErrors:0, writeOverflows:0};

function updateMock() {
  const now = Date.now(), seconds = Math.min(1, (now - mockTick) / 1000);
  mockTick = now;
  if (mock.motorOn && mock.tapeState === 'loaded' && mock.phase !== 'error') {
    if (!mockDemo) mockBytes = Math.max(0, mockBytes - Math.ceil(seconds *
      (mockType === 'prg' ? 105 : 2200)));
    mock.playedCycles = Math.min(mock.totalCycles || Infinity,
      mock.playedCycles + Math.round(seconds * mock.clockHz));
    if (!mockDemo && mockBytes === 0 && mock.fileOffsetReceived < mockSize) {
      mock.error = 'buffer_underrun'; mock.phase = 'error';
    } else if (mock.playedCycles >= mock.totalCycles) mock.phase = 'done';
    else mock.phase = 'data';
  } else if (mock.playedCycles && mock.phase !== 'error' && mock.phase !== 'done') {
    mock.phase = 'paused';
  }
  mock.playing = mock.motorOn && mock.tapeState === 'loaded' && mock.phase === 'data';
  mock.freeBytes = 8192 - mockBytes;
}

async function mockApi(path, options = {}) {
  updateMock();
  if (path === '/state') return {...mock};
  if (path === '/session') {
    const args = options.body;
    mockType = args.get('type') === 'prg' ? 'prg' : 'tap';
    const fileSize = Number(args.get('size'));
    mockSize = mockType === 'prg' ? 2 + 2 * (fileSize - 2) : fileSize;
    mockBytes = 0; mockDemo = false;
    mock.sessionId++; mock.fileOffsetReceived = mockType === 'prg' ? 2 : 20;
    mock.sourceType = mockType;
    mock.machine = mockType === 'prg' ? args.get('machine') : 'c64';
    mock.clockHz = mock.machine === 'c16' ? 886724 : 985248;
    mock.fileName = args.get('displayName') || args.get('name');
    mock.totalCycles = mockType === 'prg' ? prgTotalCycles(fileSize - 2, mock.machine) :
      Number(args.get('cycles'));
    mock.playedCycles = 0; mock.tapeState = 'ejected'; mock.phase = 'idle'; mock.error = null;
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
      throw new Error('Zatrzymaj MOTOR / nieaktualna sesja');
    const offset = Number(args.get('offset'));
    if (offset < 20 || offset >= mockSize) throw new Error('Błędny offset TAP');
    mock.sessionId++; mock.fileOffsetReceived = offset; mockBytes = 0;
    mock.tapeState = 'ejected'; mock.playedCycles = 0;
    mock.totalCycles = Number(args.get('cycles'));
    mock.phase = 'idle'; mock.error = null;
    return {sessionId:mock.sessionId, nextOffset:offset, freeBytes:8192, tapeState:'ejected'};
  }
  if (path === '/eject') {
    if (mock.motorOn) throw new Error('Zatrzymaj MOTOR');
    mock.tapeState = 'ejected'; mockBytes = 0; mock.phase = 'idle';
    mock.totalCycles = 0; mock.playedCycles = 0; source = null;
    return {tapeState:'ejected'};
  }
  if (path === '/demo') {
    if (mock.motorOn) throw new Error('Zatrzymaj MOTOR');
    mock.tapeState = 'loaded'; mock.fileName = 'draxcaught (demo)';
    mockDemo = true; mockType = 'tap'; mock.sourceType = 'tap';
    mock.machine = 'c64'; mock.clockHz = 985248;
    mock.sessionId = 0; mock.playedCycles = 0;
    mock.totalCycles = 117000000; mock.phase = 'idle'; mock.error = null;
    return {tapeState:'loaded'};
  }
  throw new Error('Nieznane żądanie: ' + path);
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

function render(s) {
  last = s;
  const playing = !!s.playing;
  $('deck').classList.toggle('playing', playing);
  $('deck').classList.toggle('ejected', s.tapeState === 'ejected');
  $('play').classList.toggle('pressed', !!s.sensePressed);
  $('loaded').textContent = s.tapeState === 'loaded' ? (s.fileName || 'Załadowana') : 'Wysunięta';
  $('motor').textContent = s.motorOn ? 'Włączony' : 'Wyłączony';
  const total = s.totalCycles || (source && source.id === s.sessionId ? source.total : null);
  $('played').textContent = time(s.playedCycles, s.clockHz) +
    (total ? ' / ' + time(total, s.clockHz) : '');
  $('remaining').textContent = total ? time(Math.max(0, total - s.playedCycles), s.clockHz) : '—';
  $('buffer').textContent = s.fileOffsetReceived ? (8192 - s.freeBytes) + ' B' : '—';
  $('phase').textContent = 'Faza: ' + s.phase + ' · odebrano ' + s.fileOffsetReceived + ' B';
  const writeLabels = {idle:'Czeka', listening:'Nasłuch', receiving_header:'Odbiera nagłówek',
    header_verified_no_action:'Nagłówek zweryfikowany', incomplete:'Niepełny nagłówek',
    capture_overflow:'Przepełniony bufor', checksum_error:'Błąd sumy',
    copy_mismatch:'Kopie różne', parity_error:'Błąd parzystości',
    sync_error:'Błąd synchronizacji', pulse_error:'Błąd impulsów',
    invalid_header:'Błędny nagłówek',
    missing_terminator:'Brak końca bloku', unexpected_terminator:'Błędny koniec bloku'};
  $('writeStatus').textContent = s.writeEnabled ?
    (writeLabels[s.writeStatus] || s.writeStatus) : 'Nieaktywny';
  $('writeDetail').textContent = s.writeStatus === 'header_verified_no_action' ?
    (s.writeMachine === 'c16' ? 'C16/Plus4' : 'C64') + ' · SAVE "' + s.writeName + '" · typ ' + s.writeType + ' · $' +
    s.writeStart.toString(16).toUpperCase().padStart(4, '0') + '–$' +
    s.writeEnd.toString(16).toUpperCase().padStart(4, '0') +
    ' · brak przypisanej akcji' :
    (s.writeEnabled ? 'Zweryfikowane nagłówki: ' + s.writeHeaders +
      ' · błędy: ' + s.writeErrors + ' · przepełnienia: ' + s.writeOverflows :
      'GPIO2/WRITE wymaga bezpiecznego układu i profilu WRITE.');
  if (s.error) $('error').textContent = 'Błąd odtwarzacza: ' + s.error;
  $('seek').disabled = !!s.motorOn || !source || source.type !== 'tap' ||
    source.id !== s.sessionId;
  const video = $('windowVideo');
  if (videoUrl && playing && video.paused) video.play().catch(() => {});
  if ((!playing || s.tapeState === 'ejected') && !video.paused) video.pause();
}

async function parseIdx(file) {
  if (!file) return [];
  if (file.size > 262144) throw Error('IDX jest zbyt duży');
  const lines = (await file.text()).replace(/^\uFEFF/, '').split(/\r?\n/);
  const entries = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line || /^[#;']/.test(line)) continue;
    const match = /^(0[xX][0-9a-fA-F]+|0[0-7]+|[1-9][0-9]*|0)[ \t]+(.+)$/.exec(line);
    if (!match) throw Error('Nieznany format IDX w wierszu ' + (i + 1));
    const token = match[1];
    const radix = /^0[xX]/.test(token) ? 16 : /^0[0-7]+$/.test(token) ? 8 : 10;
    const offset = parseInt(token, radix);
    if (!Number.isSafeInteger(offset) || offset < 20 || offset > 0xffffffff ||
        !match[2].trim())
      throw Error('Błędny offset lub opis IDX w wierszu ' + (i + 1));
    entries.push({offset, label:match[2].trim().slice(0, 120)});
    if (entries.length > 512) throw Error('IDX ma za dużo pozycji');
  }
  if (!entries.length) throw Error('IDX nie zawiera pozycji');
  return entries;
}

async function scanTape(file, indexedOffsets = []) {
  const wanted = new Set(indexedOffsets), positions = new Map();
  let position = 20, total = 0;
  while (position < file.size) {
    const bytes = new Uint8Array(await file.slice(position,
      Math.min(file.size, position + 65539)).arrayBuffer());
    const limit = Math.min(65536, file.size - position);
    let i = 0;
    while (i < limit) {
      const absolute = position + i;
      if (wanted.has(absolute)) positions.set(absolute, total);
      if (bytes[i]) { total += bytes[i] * 8; i++; }
      else {
        if (i + 3 >= bytes.length) throw Error('Ucięty impuls TAP');
        const cycles = bytes[i+1] | (bytes[i+2] << 8) | (bytes[i+3] << 16);
        if (!cycles) throw Error('Zerowy impuls TAP');
        total += cycles; i += 4;
      }
    }
    position += i;
  }
  return {total, positions};
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
  $('indexInfo').textContent = warning || entries.length + ' poprawnych pozycji';
}

function selectedFilesText() {
  $('selectedFiles').textContent = (selectedFile?.name || 'Brak pliku') +
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

async function acquire() {
  while (busy) await new Promise(resolve => setTimeout(resolve, 25));
  busy = true;
}

async function step() {
  if (busy) return;
  busy = true;
  try {
    const state = await api('/state');
    render(state);
    if (source && state.phase !== 'error' && state.sessionId === source.id) {
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
          if (error.data && Number.isInteger(error.data.nextOffset)) {
            source.offset = error.data.nextOffset;
            credit = error.data.freeBytes || 0;
          } else throw error;
        }
      }
    }
  } catch (error) {
    $('error').textContent = 'Połączenie/transfer: ' + error.message;
  } finally { busy = false; }
}

$('load').onclick = async () => {
  const file = selectedFile || $('file').files[0];
  if (!file) { $('error').textContent = 'Wybierz TAP lub PRG'; return; }
  if (last?.motorOn) { $('error').textContent = 'Zatrzymaj MOTOR'; return; }
  await acquire();
  try {
    const isPrg = /\.prg$/i.test(file.name);
    if (!isPrg && !/\.tap$/i.test(file.name)) throw Error('Wybierz TAP lub PRG');
    const bytes = new Uint8Array(await file.slice(0, isPrg ? 2 : 20).arrayBuffer());
    const header = Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
    let body, scan = null, validEntries = [], indexEntries = [];
    if (isPrg) {
      if (file.size < 3) throw Error('PRG nie zawiera danych programu');
      body = new URLSearchParams({type:'prg', size:String(file.size), header,
        name:file.name, displayName:$('programName').value || file.name.replace(/\.prg$/i, ''),
        machine:$('machine').value, turbo:$('turbo').value});
    } else {
      indexEntries = await parseIdx(selectedIdx);
      scan = await scanTape(file, indexEntries.map(entry => entry.offset));
      validEntries = indexEntries.filter(entry =>
        entry.offset >= 20 && entry.offset < file.size && scan.positions.has(entry.offset))
        .map(entry => ({...entry, prefixCycles:scan.positions.get(entry.offset)}));
      if (indexEntries.length && !validEntries.length)
        throw Error('IDX nie ma żadnej pozycji na granicy rekordu TAP');
      body = new URLSearchParams({type:'tap', size:String(file.size), header,
        name:file.name, cycles:String(scan.total)});
    }
    const ack = await api('/session', {method:'POST', body});
    source = {file, type:isPrg ? 'prg' : 'tap', id:ack.sessionId,
      offset:ack.nextOffset, logicalSize:ack.logicalSize || file.size,
      dataLength:isPrg ? file.size - 2 : 0,
      total:isPrg ? null : scan.total, fullTotal:isPrg ? null : scan.total,
      indexEntries:validEntries};
    if (isPrg) showIndex([]);
    else {
      const mismatch = selectedIdx && selectedIdx.name.replace(/\.idx$/i,'').toLowerCase() !==
        file.name.replace(/\.tap$/i,'').toLowerCase();
      showIndex(validEntries, (indexEntries.length - validEntries.length) +
        ' wpisów pominiętych (poza TAP lub wewnątrz rekordu)' +
        (mismatch ? ' · nazwy TAP/IDX różnią się' : ''));
    }
    $('error').textContent = '';
  } catch (error) {
    $('error').textContent = 'Nie można włożyć pliku: ' + error.message;
  } finally { busy = false; }
};

$('eject').onclick = async () => {
  await acquire();
  try { await api('/eject', {method:'POST'}); source = null; showIndex([]); $('error').textContent = ''; }
  catch (error) { $('error').textContent = error.message; }
  finally { busy = false; }
};
$('demo').onclick = async () => {
  await acquire();
  try { await api('/demo', {method:'POST'}); source = null; showIndex([]); $('error').textContent = ''; }
  catch (error) { $('error').textContent = error.message; }
  finally { busy = false; }
};

$('seek').onclick = async () => {
  if (!source || last?.motorOn) {
    $('error').textContent = 'Wybierz TAP i zatrzymaj MOTOR';
    return;
  }
  const offset = Number($('idxPositions').value);
  const entry = source.indexEntries.find(item => item.offset === offset);
  if (!entry) { $('error').textContent = 'Niepoprawna pozycja IDX'; return; }
  await acquire();
  try {
    const remaining = source.fullTotal - entry.prefixCycles;
    const body = new URLSearchParams({sid:String(source.id),
      offset:String(entry.offset), cycles:String(remaining)});
    const ack = await api('/seek', {method:'POST', body});
    source.id = ack.sessionId;
    source.offset = ack.nextOffset;
    source.total = remaining;
    $('error').textContent = '';
  } catch (error) {
    $('error').textContent = 'Nie można przeskoczyć: ' + error.message;
  } finally { busy = false; }
};

$('file').onchange = event => { selectedFile = event.target.files[0] || null; selectedFilesText(); updateFormatControls(); };
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
};
selectedFilesText(); updateFormatControls();

$('previewVideoFile').onchange = event => {
  if (videoUrl) URL.revokeObjectURL(videoUrl);
  const file = event.target.files[0];
  videoUrl = file ? URL.createObjectURL(file) : null;
  if (videoUrl) $('windowVideo').src = videoUrl;
  else { $('windowVideo').removeAttribute('src'); $('windowVideo').load(); }
  $('deck').classList.toggle('has-video', !!videoUrl);
  if (last) render(last);
};
if (preview) {
  $('previewPanel').hidden = false;
  $('wifiLink').hidden = true;
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
    (last?.motorOn ? 80 : 250) : 350);
}
loop();
</script></html>
)PAGE";
