import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/player.html', import.meta.url), 'utf8');
const portal = readFileSync(new URL('../src/network_config.cpp', import.meta.url), 'utf8');
assert.match(html, /<html lang="en">/);
assert.match(portal, /Forget Wi-Fi/);
assert.doesNotMatch(html + portal, /[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]/);
const script = html.split('<script>')[1].split('</script>')[0];
const elements = new Map();
function element() {
  return {
    files: [], paused: true, hidden: false, textContent: '',
    width: 640, height: 72,
    style: {setProperty(name, value){ this[name] = value; }},
    classList: {values:new Set(), add(name){this.values.add(name)}, remove(name){this.values.delete(name)},
      toggle(name, enabled){if(enabled) this.values.add(name); else this.values.delete(name);}},
    replaceChildren(){this.children = []}, append(child){(this.children ||= []).push(child)},
    play(){this.paused = false; return Promise.resolve()},
    pause(){this.paused = true}, removeAttribute(){},
    setAttribute(name, value){(this.attributes ||= {})[name] = value}, load(){},
    getContext() {
      return {clearRect(){}, fillRect(){}, beginPath(){}, moveTo(){}, lineTo(){}, stroke(){},
        fillStyle:'', strokeStyle:'', lineWidth:1};
    },
    scrollIntoView() {}
  };
}
const previewStorage = new Map();
const awakeUpdates = [];
const context = vm.createContext({
  createPlaybackAwake: () => ({update:active => awakeUpdates.push(!!active), gesture(){}}),
  localStorage: {getItem:key => previewStorage.get(key) ?? null,
    setItem:(key, value) => previewStorage.set(key, String(value))},
  document: {
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, element());
      return elements.get(id);
    },
    createElement: element
  },
  location: {protocol:'file:', search:''},
  URLSearchParams, FormData, Blob, URL, Date, Math, Number, String, Set, Map,
  Uint8Array, Promise, Error, console,
  setTimeout(fn) {
    if (typeof fn !== 'function') return 0;
    // Skip the page poll loop; still resolve acquire/demo delay promises.
    try {
      if (Function.prototype.toString.call(fn).includes('await step')) return 0;
    } catch (_) {}
    Promise.resolve().then(fn);
    return 0;
  }
});
vm.runInContext(script, context);
// Preview mode forces soft MOTOR on; unit tests need an editable (MOTOR off) deck.
vm.runInContext('mock.motorOn = false; mock.gpioDisabled = false;', context);
for (let i = 0; i < 32; i++) {
  await Promise.resolve();
  if (!vm.runInContext('busy', context)) break;
}
vm.runInContext('busy = false;', context);

const index = {size: 130, text: async () =>
  '# start\n0x14 First file\n025 Second file\n25 Third file\n; ignored\n\' ignored too\n22 Inside long pulse\n'};
const entries = await vm.runInContext('parseIdx(index)', vm.createContext({...context, index}));
assert.deepEqual(Array.from(entries, entry => entry.offset), [20, 21, 25, 22]);
assert.equal(entries[0].label, 'First file');

// Pi1551's Ultimate IDX fixture: comments, CRLF, hex, decimal, octal and
// offsets counted from the first byte of the 20-byte TAP header.
context.piIndex = {size: 160, text: async () =>
  ";comment\n# note\n' note\n0x00000015 1A. SHEEP IN SPACE\r\n" +
  '0x0003B5B9 1B. CAMELS\n20 DECIMAL ENTRY\n024 OCTAL ENTRY\n'};
const piEntries = await vm.runInContext('parseIdx(piIndex)', context);
assert.deepEqual(Array.from(piEntries, entry => entry.offset), [21, 0x3b5b9, 20, 20]);
assert.equal(piEntries[0].label, '1A. SHEEP IN SPACE');
for (const invalid of [
  '0x100000000 TOO LARGE', '0x00000010 HEADER',
  '0x14 VALID\n0x18', '029 INVALID OCTAL'
]) {
  context.badIdx = {size: invalid.length, text: async () => invalid};
  await assert.rejects(vm.runInContext('parseIdx(badIdx)', context));
}

// One short pulse at 0x14, a four-byte long pulse at 0x15, then short at 0x19.
const bytes = new Uint8Array(20 + 1 + 4 + 1);
bytes.set([0x2f, 0, 0x10, 0, 0, 0x42], 20);
const tape = new Blob([bytes]);
const scanContext = vm.createContext({...context, tape});
const scan = await vm.runInContext('scanTape(tape, [20,21,22,25])', scanContext);
assert.equal(scan.total, 47 * 8 + 16 + 66 * 8);
assert.deepEqual(Array.from(scan.positions.keys()), [20, 21, 25]);
assert.equal(scan.positions.get(25), 47 * 8 + 16);
assert.equal(scan.pulsePositions.get(20), 0);
assert.equal(scan.pulsePositions.get(21), 1);
assert.equal(scan.pulsePositions.get(25), 2);
// A C16 v2 seek at 0x15 starts on the opposite halfwave level.
assert.equal(scan.pulsePositions.get(21) % 2, 1);
for (const [machine, version] of [['C64', 1], ['C64', 2], ['C16', 1], ['C16', 2]]) {
  const header = new Uint8Array(20);
  header.set(new TextEncoder().encode(machine + '-TAPE-RAW'));
  header[12] = version;
  header[13] = machine === 'C16' ? 2 : 0;
  header[16] = 6;
  context.testHeader = header;
  const format = vm.runInContext('tapFormat(testHeader, 26)', context);
  assert.equal(format.machine, machine.toLowerCase());
  assert.equal(format.version, version);
}
context.testHeader[12] = 3;
assert.throws(() => vm.runInContext('tapFormat(testHeader, 26)', context));
const piBoundaryEntries = [20, 21, 22, 25].filter(offset => scan.positions.has(offset));
assert.deepEqual(piBoundaryEntries, [20, 21, 25]); // 22 lies inside v1 extension.

// The three extension bytes may straddle the browser's 64 KiB read boundary.
const crossing = new Uint8Array(20 + 65535 + 4 + 1);
crossing.fill(0x2f, 20, 20 + 65535);
crossing.set([0, 0x10, 0, 0, 0x42], 20 + 65535);
const crossingTape = new Blob([crossing]);
const crossingResult = await vm.runInContext(
  'scanTape(crossingTape, [65555, 65559])',
  vm.createContext({...context, crossingTape})
);
assert.equal(crossingResult.total, 65535 * 47 * 8 + 16 + 66 * 8);
assert.deepEqual(Array.from(crossingResult.positions.keys()), [65555, 65559]);
context.longTape = new Blob([new Uint8Array(20),
  Uint8Array.of(0, 0xff, 0xff, 0xff, 0x2f)]);
const longScan = await vm.runInContext('scanTape(longTape)', context);
assert.equal(longScan.timeline[1].offset, 24);
assert.equal(longScan.timeline[1].phase, 1);

context.navSource = {type:'tap', id:90, seekCycles:0, indexEntries:[], timeline:[
  {offset:20, prefixCycles:0, phase:0},
  {offset:100, prefixCycles:9852480, phase:1},
  {offset:200, prefixCycles:19704960, phase:0},
  {offset:300, prefixCycles:29557440, phase:1}]};
vm.runInContext('source=navSource; last={sessionId:90, playedCycles:4926240, clockHz:985248}', context);
assert.equal(vm.runInContext('navigationTarget(1).offset', context), 300);
assert.equal(vm.runInContext('navigationTarget(-1).offset', context), 20);
vm.runInContext(`navSource.indexEntries=[{offset:100,prefixCycles:9852480,phase:1},
  {offset:200,prefixCycles:19704960,phase:0}];
  last.playedCycles=14778720;`, context);
assert.equal(vm.runInContext('navigationTarget(1).offset', context), 200);
assert.equal(vm.runInContext('navigationTarget(-1)', context), null);
vm.runInContext('last.playedCycles=24631200;', context);
assert.equal(vm.runInContext('navigationTarget(-1).offset', context), 100);
assert.equal(vm.runInContext('navigationTarget(1)', context), null);
vm.runInContext('source=null', context);
for (const [seconds, expected] of [[0, 0], [10, 4], [30, 11],
  [60, 22], [120, 43], [600, 185], [3600, 732]])
  assert.equal(vm.runInContext(`tapeCounterAtSeconds(${seconds})`, context), expected);
assert.ok(Math.abs(vm.runInContext('secondsAtTapeCounter(100)', context) - 315) < 40);
vm.runInContext('mock.playedCycles = 60 * mock.clockHz; render({...mock})', context);
assert.equal(elements.get('counter').attributes['aria-label'], 'Tape counter 022');
assert.equal(elements.get('counterNext2').textContent, '2');
vm.runInContext('showTapeCounter(999); showTapeCounter(1000)', context);
assert.equal(elements.get('counter').attributes['aria-label'], 'Tape counter 000');

// Every changed digit rolls upward, including multi-count seeks and carries.
vm.runInContext('showTapeCounter(8); showTapeCounter(12)', context);
assert.equal(elements.get('counterOld1').textContent, '0');
assert.equal(elements.get('counterNext1').textContent, '1');
assert.equal(elements.get('counterOld2').textContent, '8');
assert.equal(elements.get('counterNext2').textContent, '2');
vm.runInContext('showTapeCounter(12)', context);
assert.equal(elements.get('counterOld2').textContent, '8'); // Polling must not reset an ongoing roll.
assert.equal(elements.get('counterNext2').textContent, '2');
vm.runInContext("renderVfd('1:23 / 4:56')", context);
assert.equal(elements.get('played').textContent, '1:23 / 4:56');
assert.equal(elements.get('vfdDigits').children.length, 9);
assert.equal(elements.get('vfdDigits').children[0].children.length, 35);
assert.equal(elements.get('vfdDigits').children[4].className, 'vfd-glyph vfd-slash');

for (const payload of [[0, 0x10, 0], [0, 0, 0, 0]]) {
  context.badTape = new Blob([new Uint8Array(20), Uint8Array.from(payload)]);
  await assert.rejects(vm.runInContext('scanTape(badTape)', context),
    /Truncated TAP pulse|Zero-length TAP pulse/);
}
context.badTape = new Blob([new Uint8Array(20), Uint8Array.of(0x2f)]);
context.badHeader = new Uint8Array(20);
context.badHeader.set(new TextEncoder().encode('C64-TAPE-RAW'));
context.badHeader[12] = 1;
context.badHeader[16] = 2; // Declares two data bytes, Blob has one.
assert.throws(() => vm.runInContext('tapFormat(badHeader, badTape.size)', context),
  /TAP data length/);

context.recoveryFile = {type:'tap', id:41, offset:20, logicalSize:128};
context.recoveryState = {sourceType:'tap', sessionId:41, logicalSize:128,
  fileOffsetReceived:45, phase:'idle'};
let recovery = vm.runInContext('reconcileSource(recoveryFile, recoveryState)', context);
assert.equal(recovery.ok, true);
assert.equal(recovery.changed, true);
assert.equal(context.recoveryFile.offset, 45); // Lost POST acknowledgement, ESP accepted bytes.
context.recoveryState.sessionId = 42;
recovery = vm.runInContext('reconcileSource(recoveryFile, recoveryState)', context);
assert.equal(recovery.reason, 'session_changed');
assert.equal(context.recoveryFile.offset, 45); // Never send old file into another session.
context.recoveryState.sessionId = 41;
context.recoveryState.phase = 'error';
assert.equal(vm.runInContext('reconcileSource(recoveryFile, recoveryState)', context).reason,
  'playback_error');
context.recoveryState.phase = 'idle';
context.recoveryState.fileOffsetReceived = 129;
assert.equal(vm.runInContext('reconcileSource(recoveryFile, recoveryState)', context).reason,
  'invalid_offset');

const created = await vm.runInContext(
  `mockApi('/session', {body:new URLSearchParams({size:'26', name:'sample.tap', cycles:'920'})})`,
  context
);
const jumped = await vm.runInContext(
  `mockApi('/seek', {body:new URLSearchParams({sid:'${created.sessionId}', offset:'25', cycles:'528'})})`,
  context
);
assert.equal(jumped.nextOffset, 25);
assert.notEqual(jumped.sessionId, created.sessionId);
for (const offset of [19, 26]) {
  await assert.rejects(vm.runInContext(
    `mockApi('/seek', {body:new URLSearchParams({sid:'${jumped.sessionId}',
      offset:'${offset}', cycles:'100', phase:'0'})})`, context), /Invalid TAP offset/);
  assert.equal(vm.runInContext('mock.sessionId', context), jumped.sessionId);
}
await assert.rejects(vm.runInContext(
  `mockApi('/chunk?sid=${created.sessionId}&offset=25', {body:new FormData()})`, context
), /offset/);

await assert.rejects(
  vm.runInContext('parseIdx(bad)', vm.createContext({...context,
    bad:{size:10, text:async()=> '0xGG Unsupported'}})),
  /Unknown IDX format/
);

// A PRG stays in one browser File. Logical offsets for the two ROM copies
// must produce the same bytes without retaining a full copy on the ESP.
context.testPrg = {
  file: new Blob([Uint8Array.of(0x01, 0x08, 0xaa, 0xbb, 0xcc)]),
  type: 'prg', dataLength: 3, logicalSize: 8
};
for (const [offset, limit, expected] of [
  [2, 1024, [0xaa, 0xbb, 0xcc]],
  [4, 1024, [0xcc]],
  [5, 1024, [0xaa, 0xbb, 0xcc]],
  [6, 1, [0xbb]]
]) {
  const chunk = vm.runInContext(`sourceChunk(testPrg, ${offset}, ${limit})`, context);
  assert.deepEqual(Array.from(new Uint8Array(await chunk.arrayBuffer())), expected);
}
const prgSession = await vm.runInContext(
  `mockApi('/session', {body:new URLSearchParams({type:'prg', size:'5',
    header:'0108', machine:'c64', turbo:'0', name:'test.prg'})})`, context
);
assert.equal(prgSession.nextOffset, 2);
assert.equal(prgSession.logicalSize, 8);
assert.equal(vm.runInContext('prgTotalCycles(4) - prgTotalCycles(3)', context),
  2 * (680 + 536 + 9 * (384 + 536)));
assert.equal(vm.runInContext("prgTotalCycles(4, 'c16') - prgTotalCycles(3, 'c16')", context),
  2 * (1792 + 896 + 9 * (448 + 896)));
const c16Session = await vm.runInContext(
  `mockApi('/session', {body:new URLSearchParams({type:'prg', size:'5',
    header:'0110', machine:'c16', turbo:'0', name:'test-c16.prg'})})`, context
);
assert.equal(c16Session.logicalSize, 8);
assert.equal(vm.runInContext('mock.clockHz', context), 886724);
await vm.runInContext("mockApi('/autoplay', {body:new URLSearchParams({enabled:'0'})})", context);
const manualSession = await vm.runInContext(
  `mockApi('/session', {body:new URLSearchParams({type:'prg', size:'5',
    header:'0108', machine:'c64', turbo:'0', name:'manual.prg'})})`, context
);
assert.ok(manualSession.sessionId);
assert.equal(vm.runInContext('mock.sensePressed', context), false);
await vm.runInContext("document.getElementById('play').onclick()", context);
assert.equal(vm.runInContext('mock.sensePressed', context), true);
await vm.runInContext("document.getElementById('stop').onclick()", context);
assert.equal(vm.runInContext('mock.sensePressed', context), false);
await vm.runInContext("document.getElementById('autoPlay').checked = true; document.getElementById('autoPlay').onchange()", context);
assert.equal(vm.runInContext('mock.autoPlay', context), true);
vm.runInContext("document.getElementById('defaultMachine').value = 'c16'", context);
await vm.runInContext("document.getElementById('defaultMachine').onchange()", context);
assert.equal(vm.runInContext('mock.defaultMachine', context), 'c16-pal');
assert.equal(previewStorage.get('espsette.preview.machine'), 'c16-pal');
await assert.rejects(vm.runInContext("mockApi('/default-machine', {body:new URLSearchParams({machine:'vic20'})})", context));
const resume = await vm.runInContext(
  `mockApi('/session', {body:new URLSearchParams({type:'tap', size:'26',
    name:'resume.tap', cycles:'920'})})`, context
);
context.resumeTape = tape;
vm.runInContext(`source = {file:resumeTape, type:'tap', id:${resume.sessionId},
  offset:20, logicalSize:26, total:920, fullTotal:920, seekCycles:0, indexEntries:[],
  timeline:[{offset:20, prefixCycles:0, phase:0}]};
  originalApi = api; api = async () => { throw Error('offline'); };`, context);
await vm.runInContext('step()', context);
assert.equal(vm.runInContext('source.offset', context), 20);
assert.match(elements.get('transport').textContent, /Connection lost/);
assert.equal(awakeUpdates.at(-1),false);
vm.runInContext('api = originalApi; mock.fileOffsetReceived = 21; mockBytes = 1;', context);
await vm.runInContext('step()', context);
assert.equal(vm.runInContext('source.offset', context), 26);
assert.equal(vm.runInContext('mock.fileOffsetReceived', context), 26);
assert.match(elements.get('transport').textContent, /Connection restored/);
await vm.runInContext("document.getElementById('rewind').onclick()", context);
assert.equal(vm.runInContext('source.offset', context), 20);
assert.equal(vm.runInContext('mock.fileOffsetReceived', context), 20);
vm.runInContext(`source.timeline.push({offset:25, prefixCycles:10, phase:0});
  last.clockHz=1;`, context);
await vm.runInContext("document.getElementById('fastForward').onclick()", context);
assert.equal(vm.runInContext('source.offset', context), 25);
assert.equal(vm.runInContext('mock.fileOffsetReceived', context), 25);
vm.runInContext('mock.sessionId++', context);
await vm.runInContext('step()', context);
assert.equal(vm.runInContext('source', context), null);
assert.match(elements.get('transport').textContent, /no source file/);
await vm.runInContext("document.getElementById('demo').onclick()", context);
vm.runInContext('mock.playedCycles = 1234; render({...mock})', context);
await vm.runInContext("document.getElementById('rewind').onclick()", context);
assert.equal(vm.runInContext('mock.playedCycles', context), 0);
assert.equal(vm.runInContext('mock.sensePressed', context), true);

vm.runInContext("mock.tapeState = 'loaded'; mock.fileName = 'game.tap'; mock.machine = 'c16'; mock.sourceType = 'tap'; render({...mock})", context);
assert.match(elements.get('loaded').textContent, /game\.tap/);
assert.equal(elements.get('machineLabel').textContent, 'C16');
assert.equal(elements.get('standardPAL').attributes['data-active'], 'true');
assert.equal(elements.get('standardNTSC').attributes['data-active'], 'false');
assert.equal(elements.get('machineC16').attributes['data-active'], 'true');
assert.equal(elements.get('machineC64').attributes['data-active'], 'false');
vm.runInContext("mock.machine = 'c64'; render({...mock})", context);
assert.equal(elements.get('machineLabel').textContent, 'C64');
assert.equal(elements.get('machineC64').attributes['data-active'], 'true');
assert.equal(elements.get('machineC16').attributes['data-active'], 'false');
// Choosing a TAP inserts it immediately; IDX can be attached and removed
// without replacing the session or rewinding playback.
const uiHeader = new Uint8Array(20);
uiHeader.set(new TextEncoder().encode('C64-TAPE-RAW'));
uiHeader[12] = 2;
uiHeader[16] = 6;
context.uiTape = new Blob([uiHeader, Uint8Array.of(0x2f, 0, 0x10, 0, 0, 0x42)]);
context.uiTape.name = 'ui.tap';
context.uiIndex = {name:'ui.idx', size:20, text:async () => '20 Start\n25 Next'};
vm.runInContext('mock.motorOn = false; mock.gpioDisabled = false; render({...mock})', context);
await vm.runInContext('chooseFiles([uiTape])', context);
await vm.runInContext('step()', context);
assert.equal(vm.runInContext('source.file', context), context.uiTape);
assert.equal(elements.get('selectedFiles').hidden, true);
assert.match(elements.get('loaded').textContent, /ui.tap/);
const uiSession = vm.runInContext('source.id', context);
const uiOffset = vm.runInContext('source.offset', context);
await vm.runInContext('attachIndex(uiIndex)', context);
assert.equal(vm.runInContext('source.id', context), uiSession);
assert.equal(vm.runInContext('source.offset', context), uiOffset);
assert.equal(vm.runInContext('source.indexEntries.length', context), 2);
assert.equal(vm.runInContext('source.indexEntries[1].phase', context), 0);
assert.equal(vm.runInContext('selectedIdx.name', context), 'ui.idx');
assert.equal(elements.get('indexControls').hidden, false);
context.uiBadIndex = {name:'bad.idx', size:10, text:async () => '22 Inside pulse'};
await vm.runInContext('attachIndex(uiBadIndex)', context);
assert.match(elements.get('error').textContent, /Cannot attach index/);
assert.equal(vm.runInContext('source.indexEntries.length', context), 2);
assert.equal(vm.runInContext('selectedIdx.name', context), 'ui.idx');
await vm.runInContext('attachIndex(null)', context);
assert.equal(vm.runInContext('source.indexEntries.length', context), 0);
assert.equal(elements.get('indexControls').hidden, true);
assert.equal(vm.runInContext('selectedIdx', context), null);
await vm.runInContext('chooseFiles([uiTape, uiIndex])', context);
assert.equal(vm.runInContext('source.indexEntries.length', context), 2);
// Replacing the tape discards the previous tape's optional index.
await vm.runInContext('chooseFiles([uiTape])', context);
assert.equal(vm.runInContext('selectedIdx', context), null);
assert.equal(vm.runInContext('source.indexEntries.length', context), 0);
// PRG uses the saved computer and filename, and is inserted immediately.
context.uiPrg = new Blob([Uint8Array.of(1, 8, 42)]);
context.uiPrg.name = 'program.PRG';
await vm.runInContext('chooseFiles([uiPrg])', context);
assert.equal(vm.runInContext('source.type', context), 'prg');
assert.equal(vm.runInContext('mock.machine', context), 'c16');
assert.equal(vm.runInContext('mock.fileName', context), 'program');
assert.equal(elements.get('indexControls').hidden, true);
assert.equal(elements.get('selectedFiles').hidden, true);
// Both orders of TAP + IDX work. Only the first supported tape is used.
await vm.runInContext('chooseFiles([uiIndex, uiTape])', context);
assert.equal(vm.runInContext('source.indexEntries.length', context), 2);
await vm.runInContext('chooseFiles([uiPrg, uiTape, uiIndex])', context);
assert.equal(vm.runInContext('source.file', context), context.uiPrg);
assert.equal(vm.runInContext('selectedIdx', context), null);
await vm.runInContext('chooseFiles([uiTape, uiPrg, uiIndex])', context);
assert.equal(vm.runInContext('source.file', context), context.uiTape);
assert.equal(vm.runInContext('source.indexEntries.length', context), 2);
// Demo must replace the file while preview's soft MOTOR is on.
vm.runInContext('mock.motorOn = true; mock.gpioDisabled = true; render({...mock})', context);
await vm.runInContext("document.getElementById('demo').onclick()", context);
assert.match(elements.get('loaded').textContent, /draxcaught \(demo\)/);
assert.equal(vm.runInContext('source', context), null);
assert.equal(vm.runInContext('selectedFile', context), null);
assert.equal(elements.get('error').textContent, '');
assert.equal(elements.get('bufferIcon').attributes['data-active'], 'false');
vm.runInContext('mock.embedded = false; mock.sessionId = 1; mock.freeBytes = 4096; render({...mock})', context);
assert.equal(elements.get('bufferIcon').attributes['data-active'], 'true');
assert.equal(elements.get('bufferIcon').attributes['data-level'], '4');
vm.runInContext('mock.freeBytes = 0; render({...mock})', context);
assert.equal(elements.get('bufferIcon').attributes['data-level'], '8');
// Real MOTOR still blocks editing; the error belongs to playback status.
vm.runInContext('mock.gpioDisabled = false; render({...mock})', context);
await vm.runInContext("document.getElementById('demo').onclick()", context);
assert.match(elements.get('error').textContent, /Stop MOTOR/);
assert.match(html, /class="playback-status"[^>]*>[\s\S]*?id="error"/);
vm.runInContext('mock.motorOn = false; render({...mock})', context);
await vm.runInContext("document.getElementById('deckEject').onclick()", context);
assert.equal(vm.runInContext('selectedFile', context), null);
assert.equal(elements.get('loaded').textContent, 'Ejected');
assert.equal(elements.get('played').textContent, '00:00 / --:--');
assert.equal(elements.get('vfdDigits').children.length, 11);
assert.equal(elements.get('selectedFiles').hidden, true);
// Shipped manual fixtures must load together and expose the advertised markers.
context.fixtureTape = new Blob([readFileSync(new URL('fixtures/ui-test.tap', import.meta.url))]);
context.fixtureTape.name = 'ui-test.tap';
context.fixtureIndex = new Blob([readFileSync(new URL('fixtures/ui-test.idx', import.meta.url))]);
context.fixtureIndex.name = 'ui-test.idx';
vm.runInContext('mock.motorOn=false; render({...mock})', context);
await vm.runInContext('chooseFiles([fixtureIndex, fixtureTape])', context);
assert.equal(elements.get('error').textContent, '');
assert.equal(vm.runInContext('source.file', context), context.fixtureTape);
assert.ok(vm.runInContext('source.logicalSize', context) > 8192);
assert.equal(vm.runInContext('source.fullTotal', context), 120 * 985248);
assert.deepEqual(Array.from(vm.runInContext('source.indexEntries', context),
  entry => entry.prefixCycles), [0, 20, 40, 60, 80, 100].map(seconds => seconds * 985248));
await vm.runInContext('seekTo(source.indexEntries[3])', context);
assert.equal(vm.runInContext('source.seekCycles', context), 60 * 985248);
assert.equal(elements.get('error').textContent, '');
// IDX navigation follows actual playback boundaries, not received/upload bytes.
await vm.runInContext('step()', context);
await vm.runInContext('step()', context);
vm.runInContext('mock.playedCycles=0; source.seekCycles=0; render({...mock})', context);
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[0].offset', context)));
assert.equal(elements.get('rewindLabel').textContent, 'PREV');
assert.equal(elements.get('forwardLabel').textContent, 'NEXT');
assert.equal(elements.get('rewind').disabled, true);
assert.equal(elements.get('idxPositions').children[0].textContent, '00:00 Start');
assert.equal(elements.get('loaded').textContent, 'Start');
assert.equal(elements.get('played').textContent, '00:00 / 02:00');
vm.runInContext('mock.playedCycles=40*mock.clockHz-1; render({...mock})', context);
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[1].offset', context)));
vm.runInContext('mock.playedCycles=40*mock.clockHz; render({...mock})', context);
assert.equal(elements.get('loaded').textContent, 'Third section');
assert.equal(elements.get('played').textContent, '00:40 / 02:00');
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[2].offset', context)));
vm.runInContext('mock.playedCycles=119*mock.clockHz; render({...mock})', context);
assert.equal(elements.get('fastForward').disabled, true);
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[5].offset', context)));
await vm.runInContext("document.getElementById('rewind').onclick()", context);
assert.equal(vm.runInContext('source.seekCycles', context), 80*985248);
assert.equal(elements.get('loaded').textContent, 'Fifth section');
assert.equal(elements.get('played').textContent, '01:20 / 02:00');
// While choosing a Jump destination, polling must not overwrite the choice.
context.document.activeElement=elements.get('idxPositions');
vm.runInContext("document.getElementById('idxPositions').value=String(source.indexEntries[1].offset); document.getElementById('idxPositions').onchange(); render({...mock})", context);
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[1].offset', context)));
context.document.activeElement=elements.get('seek');
await vm.runInContext("document.getElementById('seek').onclick()", context);
assert.equal(vm.runInContext('source.seekCycles', context), 20*985248);
assert.equal(elements.get('loaded').textContent, 'Second section');
assert.equal(elements.get('played').textContent, '00:20 / 02:00');
assert.equal(elements.get('remaining').textContent, '1:40');
vm.runInContext('mock.playedCycles=20*mock.clockHz; render({...mock})', context);
assert.equal(elements.get('loaded').textContent, 'Third section');
assert.equal(elements.get('played').textContent, '00:40 / 02:00');
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[2].offset', context)));
vm.runInContext('mock.playedCycles=0; render({...mock})', context);
context.document.activeElement=null;
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[1].offset', context)));
await vm.runInContext('chooseFiles([fixtureTape])', context);
assert.equal(elements.get('indexControls').hidden,true);
assert.equal(elements.get('rewindLabel').textContent,'REW');
assert.equal(elements.get('forwardLabel').textContent,'FF');
// The deck and picker accept the same file drops, including late IDX.
context.deckDrop = {preventDefault(){this.prevented=true}, stopPropagation(){this.stopped=true},
  dataTransfer:{files:[context.fixtureIndex, context.fixtureTape]}};
await vm.runInContext("document.getElementById('deck').ondrop(deckDrop)", context);
assert.equal(context.deckDrop.prevented,true);
assert.equal(context.deckDrop.stopped,true);
assert.equal(vm.runInContext('source.file',context), context.fixtureTape);
assert.equal(vm.runInContext('source.indexEntries.length',context),6);
context.pickerDrop = {...context.deckDrop, dataTransfer:{files:[context.fixtureTape]}};
await vm.runInContext("document.getElementById('dropzone').ondrop(pickerDrop)",context);
assert.equal(vm.runInContext('source.indexEntries.length',context),0);
const beforeLateDrop=vm.runInContext('source.id',context);
context.deckDrop.dataTransfer.files=[context.fixtureIndex];
await vm.runInContext("document.getElementById('deck').ondrop(deckDrop)",context);
assert.equal(vm.runInContext('source.id',context), beforeLateDrop);
assert.equal(vm.runInContext('source.indexEntries.length',context),6);
context.deckDrop.dataTransfer.files=[context.uiPrg];
await vm.runInContext("document.getElementById('deck').ondrop(deckDrop)",context);
assert.equal(vm.runInContext('source.type',context),'prg');
assert.equal(elements.get('indexControls').hidden,true);
// All four saved PRG profiles select the real tape clock and VFD indicators.
for (const [profile, machine, video, hz] of [
  ['c64-pal','c64','pal',985248], ['c64-ntsc','c64','ntsc',1022730],
  ['c16-pal','c16','pal',886724], ['c16-ntsc','c16','ntsc',894886]
]) {
  context.profileChoice=profile;
  await vm.runInContext("document.getElementById('defaultMachine').value=profileChoice; document.getElementById('defaultMachine').onchange()",context);
  await vm.runInContext('chooseFiles([uiPrg])',context);
  assert.equal(vm.runInContext('mock.defaultMachine',context), profile);
  assert.equal(vm.runInContext('source.sessionBody.get("machine")',context), machine);
  assert.equal(vm.runInContext('source.sessionBody.get("video")',context), video);
  assert.equal(vm.runInContext('mock.clockHz',context), hz);
  assert.equal(elements.get('standardLabel').textContent,video.toUpperCase());
  assert.equal(elements.get(video==='ntsc'?'standardNTSC':'standardPAL').attributes['data-active'],'true');
}
// TAP headers override the PRG default; validate both versions for each profile.
for (const [machine, video, hz] of [
  ['C64',0,985248], ['C64',1,1022730], ['C16',0,886724], ['C16',1,894886]
]) for (const version of [1,2]) {
  const header=new Uint8Array(20);
  header.set(new TextEncoder().encode(machine+'-TAPE-RAW'));
  header[12]=version;header[13]=machine==='C16'?2:0;header[14]=video;header[16]=1;
  context.profileHeader=header;
  const parsed=vm.runInContext('tapFormat(profileHeader,21)',context);
  assert.equal(parsed.clockHz,hz);
  assert.equal(parsed.video,video?'ntsc':'pal');
  context.profileTape=new Blob([header,Uint8Array.of(48)]);
  context.profileTape.name='clock.tap';
  await vm.runInContext('chooseFiles([profileTape]);',context);
  assert.equal(vm.runInContext('mock.clockHz',context),hz);
  assert.equal(elements.get('standardLabel').textContent,video?'NTSC':'PAL');
}
context.profileHeader[14]=2;
assert.throws(()=>vm.runInContext('tapFormat(profileHeader,21)',context),/PAL\/NTSC/);
await assert.rejects(vm.runInContext("mockApi('/default-machine',{body:new URLSearchParams({machine:'c16-secam'})})",context));
// Legacy preview settings migrate to PAL without losing the computer choice.
previewStorage.set('espsette.preview.machine','c16');
// Screen protection follows actual playback, even after the whole file is buffered.
vm.runInContext("mock.motorOn=true; mock.sensePressed=true; mock.playing=true; mock.tapeState='loaded';mock.phase='data';mock.error=null;render({...mock})", context);
assert.equal(awakeUpdates.at(-1),true);
vm.runInContext('render({...mock,fileOffsetReceived:mock.logicalSize})',context);
assert.equal(awakeUpdates.at(-1),true);
vm.runInContext('render({...mock, motorOn:false})',context);
assert.equal(awakeUpdates.at(-1),false);
vm.runInContext('render({...mock, sensePressed:false})',context);
assert.equal(awakeUpdates.at(-1),false);
vm.runInContext("render({...mock, phase:'done'})",context);
assert.equal(awakeUpdates.at(-1),false);
vm.runInContext("render({...mock, phase:'error',error:'buffer_underrun'})",context);
assert.equal(awakeUpdates.at(-1),false);
vm.runInContext('mock.motorOn=false;mock.playing=false;render({...mock})',context);
// Fast PRG uses MOTOR/SENSE as protocol signals, including waits with playing=false.
for (const phase of ['bootstrap', 'fastload']) {
  for (const motorOn of [false, true]) for (const sensePressed of [false, true]) {
    context.fastWakeState = {sourceType:'prg', fastloaderActive:true, tapeState:'loaded',
      phase, motorOn, sensePressed, playing:false, error:null};
    vm.runInContext('render({...mock, ...fastWakeState})', context);
    assert.equal(awakeUpdates.at(-1), true, `${phase}: MOTOR=${motorOn}, SENSE=${sensePressed}`);
  }
}
for (const phase of ['header', 'headerPause', 'data', 'paused']) {
  context.fastWakeState = {sourceType:'prg', fastloaderActive:true, tapeState:'loaded',
    phase, motorOn:false, sensePressed:true, playing:false, error:null};
  vm.runInContext('render({...mock, ...fastWakeState})', context);
  assert.equal(awakeUpdates.at(-1), true); // Keep awake across ordinary bootstrap MOTOR pauses.
}
for (const override of [
  {phase:'done'}, {phase:'error'}, {error:'fastloader_cancelled'}, {tapeState:'ejected'},
  {phase:'idle'}, {phase:'paused', sensePressed:false},
  {sourceType:'tap', phase:'fastload'}, {fastloaderActive:false, phase:'fastload'}
]) {
  context.fastWakeOverride = override;
  vm.runInContext('render({...mock, ...fastWakeState, ...fastWakeOverride})', context);
  assert.equal(awakeUpdates.at(-1), false, JSON.stringify(override));
}
vm.runInContext('render({...mock})', context);
// WRITE requests are received metadata, never presented as executed commands.
vm.runInContext("render({...mock, writeEnabled:true, writeStatus:'command_received_no_action', writeCommand:'TEST', writeCommandId:3, writeHeaders:5, writeErrors:1, writeOverflows:0})", context);
assert.equal(elements.get('writeStatus').textContent, 'Request received (not executed)');
assert.match(elements.get('writeDetail').textContent, /#3: ESP:TEST/);
assert.match(elements.get('writeDetail').textContent, /Verified headers: 5/);
vm.runInContext("render({...mock, writeEnabled:true, writeStatus:'suspended_fastloader'})", context);
assert.equal(elements.get('writeStatus').textContent, 'Paused for fastloader');
vm.runInContext('render({...mock, writeEnabled:false})', context);
assert.equal(elements.get('writeStatus').textContent, 'Disabled');
assert.equal(elements.get('writeDetail').textContent, '');
vm.runInContext('render({...mock})', context);
// Fastload progress uses transmitted payload bytes, measured time and virtual ROM duration.
context.progressState = {sourceType:'prg', sessionId:98765, fileName:'FAST.PRG',
  fastloaderActive:true, fastloaderTotalBytes:4096, fastloaderBytes:0,
  fastloaderElapsedMs:0, fastloaderWaitingForData:false,
  clockHz:985248, machine:'c64', tapeState:'loaded', phase:'fastload',
  motorOn:false, sensePressed:false, playing:true, error:null};
vm.runInContext(`progressState.playedCycles = prgTotalCycles(2) + 200*384;
  progressState.totalCycles = progressState.playedCycles + prgDataCycles(4096);
  render({...mock,...progressState})`, context);
const fastCounterStart = vm.runInContext('lastCounterValue', context);
vm.runInContext(`progressState.fastloaderBytes=1024;progressState.fastloaderElapsedMs=125;
  progressState.playedCycles += prgDataCycles(1024); render({...mock,...progressState})`, context);
assert.equal(elements.get('fastProgress').hidden, false);
assert.equal(elements.get('fastProgressBar').max, 4096);
assert.equal(elements.get('fastProgressBar').value, 1024);
assert.equal(elements.get('fastProgressBytes').textContent, '1,024 / 4,096 B · 25%');
assert.match(elements.get('fastProgressDetail').textContent, /3,072 B left/);
assert.match(elements.get('fastProgressDetail').textContent, /8.19 kB\/s average/);
assert.equal(elements.get('remainingLabel').textContent, 'Transfer remaining');
assert.equal(elements.get('remaining').textContent, '3,072 B · ~0.4 s');
assert.ok(vm.runInContext('lastCounterValue', context) > fastCounterStart);
assert.equal(elements.get('deck').classList.values.has('fastloading'), true);
const normalReelPeriod = Number(html.match(/\.playing \.reel\{animation:spin ([\d.]+)s/)[1]);
const fastReelPeriod = Number(html.match(/\.playing\.fastloading \.reel\{animation-duration:([\d.]+)s/)[1]);
assert.ok(fastReelPeriod * 4 < normalReelPeriod); // Turbo must be clearly faster, including before rate telemetry.
assert.equal(vm.runInContext('fastloaderView({...progressState,fastloaderBytes:0,fastloaderElapsedMs:0}).spinning',context),true);
assert.equal(vm.runInContext("fastloaderView({...progressState,phase:'bootstrap',fastloaderBytes:0,fastloaderElapsedMs:0}).spinning",context),true);
vm.runInContext('render({...mock,...progressState,fastloaderWaitingForData:true,playing:false})', context);
assert.match(elements.get('fastProgressDetail').textContent, /Waiting for data/);
assert.equal(elements.get('deck').classList.values.has('playing'), false);
assert.equal(awakeUpdates.at(-1), true); // A data stall still needs screen protection.
vm.runInContext('render({...mock,...progressState,fastloaderWaitingForMotor:true,playing:false})', context);
assert.match(elements.get('fastProgressDetail').textContent, /Waiting for receiver/);
assert.equal(elements.get('deck').classList.values.has('playing'), false);
assert.equal(awakeUpdates.at(-1), true);
vm.runInContext(`render({...mock,...progressState,fastloaderBytes:4096,phase:'done',playing:false,
  playedCycles:progressState.totalCycles})`, context);
assert.equal(elements.get('fastProgressBar').value, 4096);
assert.match(elements.get('fastProgressBytes').textContent, /100%/);
assert.match(elements.get('fastProgressDetail').textContent, /^Complete · 0 B left/);
assert.equal(elements.get('deck').classList.values.has('fastloading'), false);
assert.equal(awakeUpdates.at(-1), false);
assert.equal(vm.runInContext('fastloaderView({...progressState,fastloaderBytes:5000}).sent',context),4096);
assert.equal(vm.runInContext('fastloaderView({...progressState,fastloaderBytes:-2}).sent',context),0);
assert.equal(vm.runInContext('fastloaderView({...progressState,fastloaderElapsedMs:0}).eta',context),null);
assert.equal(vm.runInContext('fastloaderView({...progressState,fastloaderWaitingForData:true}).eta',context),null);
assert.equal(vm.runInContext('fastloaderView({...progressState,fastloaderTotalBytes:0})',context),null);
vm.runInContext(`render({...mock,sourceType:'tap',fastloaderActive:false})`, context);
assert.equal(elements.get('fastProgress').hidden, true);
assert.equal(elements.get('remainingLabel').textContent, 'Signal remaining');
assert.equal(elements.get('deck').classList.values.has('fastloading'), false);
assert.equal(vm.runInContext('pollDelay(progressState,false)',context),100); // Fully uploaded PRG still polls quickly.
assert.equal(vm.runInContext("pollDelay({sourceType:'tap',playing:false},false)",context),350);
assert.equal(vm.runInContext("pollDelay({sourceType:'tap',playing:true},true)",context),80);
vm.runInContext(`render({...mock,writeEnabled:true,writeName:'NAZWA',writeCommand:'',
  writeHeaders:1,writeStatus:'header_ignored'})`, context);
assert.match(elements.get('writeDetail').textContent, /Last header: "NAZWA"/);
assert.equal(elements.get('writeStatus').textContent, 'Non-command header ignored');
vm.runInContext(`render({...mock,freeHeap:23000,freeStack:1024,resetReason:3})`, context);
assert.match(elements.get('diagnostics').textContent, /heap 23000 B/);
assert.match(elements.get('diagnostics').textContent, /stack watermark 1024 B/);
assert.match(elements.get('diagnostics').textContent, /last reset: software watchdog/);
vm.runInContext(`render({...mock,freeHeap:23000,resetReason:2,resetException:28,resetPc:0x40201234})`, context);
assert.match(elements.get('diagnostics').textContent, /exception \(cause 28 @0x40201234\)/);

// Preview uses the configured fast rate, waits for refill and ignores MOTOR/SENSE as data controls.
vm.runInContext(`globalThis.progressSnapshot = {state:{...mock},bytes:mockBytes,bootstrap:mockBootstrapCycles};
  Object.assign(mock,progressState,{phase:'fastload',fastloaderBytes:0,fastloaderElapsedMs:0,
    fastloaderTotalBytes:16384,playedCycles:1000,fastloaderWaitingForData:false});
  mockBootstrapCycles=1000;mockBytes=0;advanceMockFast(.25)`,context);
assert.equal(vm.runInContext('mock.fastloaderBytes',context),0);
assert.equal(vm.runInContext('mock.fastloaderWaitingForData',context),true);
assert.equal(vm.runInContext('mock.error',context),null);
vm.runInContext('mockBytes=8192;advanceMockFast(.1)',context);
assert.ok(vm.runInContext('mock.fastloaderBytes',context)>800);
assert.ok(vm.runInContext('mock.fastloaderBytes',context)<1100);
vm.runInContext('mock.fastloaderTotalBytes=mock.fastloaderBytes+100;mockBytes=100;advanceMockFast(1)',context);
assert.equal(vm.runInContext('mock.phase',context),'done');
assert.equal(vm.runInContext('mock.playing',context),false);
vm.runInContext(`for(const key of Object.keys(mock)) delete mock[key];
  Object.assign(mock,progressSnapshot.state);mockBytes=progressSnapshot.bytes;
  mockBootstrapCycles=progressSnapshot.bootstrap;render({...mock})`,context);
// Persistent fastloader settings: validation, dirty edits, defaults, activation.
vm.runInContext("mock.motorOn = false; mock.gpioDisabled = false; mock.phase = 'idle'; render({...mock})", context);
assert.equal(elements.get('fastloaderEnabled').checked, false);
assert.equal(elements.get('fast_holdC64Us').value, '16');
elements.get('fast_holdC64Us').value = '45';
elements.get('fast_holdC64Us').oninput();
vm.runInContext('render({...mock})', context);
assert.equal(elements.get('fast_holdC64Us').value, '45'); // Polling preserves edits.
elements.get('fastloaderEnabled').checked = true;
await elements.get('fastloaderEnabled').onchange();
assert.equal(vm.runInContext('mock.fastloader.enabled', context), true);
assert.equal(vm.runInContext('mock.fastloader.holdC64Us', context), 16); // Toggle ignores unsaved numbers.
assert.equal(elements.get('fast_holdC64Us').value, '45');
assert.equal(JSON.parse(previewStorage.get('espsette.preview.fastloader')).enabled, true);
// The numeric Save button must not read a stale/unsaved switch value.
elements.get('fastloaderEnabled').checked = false;
await elements.get('fastloaderSave').onclick();
assert.equal(vm.runInContext('mock.fastloader.enabled', context), true);
assert.equal(elements.get('fastloaderEnabled').checked, true);
assert.equal(vm.runInContext('mock.fastloader.holdC64Us', context), 45);
assert.equal(JSON.parse(previewStorage.get('espsette.preview.fastloader')).holdC64Us, 45);
vm.runInContext('render({...mock, motorOn:true})', context);
assert.equal(elements.get('fastloaderSave').disabled, true);
vm.runInContext("render({...mock, motorOn:false, phase:'fastload'})", context);
assert.equal(elements.get('fastloaderSave').disabled, true);
vm.runInContext('render({...mock})', context);
elements.get('fast_holdC64Us').value = '-1';
elements.get('fast_holdC64Us').oninput();
elements.get('fastloaderEnabled').checked = false;
await elements.get('fastloaderEnabled').onchange();
assert.equal(vm.runInContext('mock.fastloader.enabled', context), false);
assert.equal(vm.runInContext('mock.fastloader.holdC64Us', context), 45);
assert.equal(elements.get('fast_holdC64Us').value, '-1'); // Even invalid edits do not block the switch.
elements.get('fastloaderEnabled').checked = true;
await elements.get('fastloaderEnabled').onchange();
assert.equal(vm.runInContext('mock.fastloader.enabled', context), true);
assert.equal(elements.get('fast_holdC64Us').value, '-1');
await elements.get('fastloaderSave').onclick();
assert.match(elements.get('error').textContent, /Cannot save fastloader/);
assert.equal(vm.runInContext('mock.fastloader.holdC64Us', context), 45);
elements.get('fastloaderDefaults').onclick();
assert.equal(elements.get('fast_holdC64Us').value, '16');
assert.equal(elements.get('fastloaderEnabled').checked, true);
await elements.get('fastloaderSave').onclick();
context.fastPrg = new Blob([Uint8Array.of(1,8,0,0)]); context.fastPrg.name = 'fast.prg';
await vm.runInContext('chooseFiles([fastPrg])', context);
assert.equal(vm.runInContext('source.sessionBody.get("turbo")', context), '1');
assert.equal(vm.runInContext('source.logicalSize', context), 4); // One PRG copy.
const fastSessionBeforeSwitch = vm.runInContext('source.id',context);
elements.get('fastloaderEnabled').checked = false;
await elements.get('fastloaderEnabled').onchange();
assert.notEqual(vm.runInContext('source.id',context),fastSessionBeforeSwitch);
assert.equal(vm.runInContext('source.file',context),context.fastPrg); // Same inserted PRG, new mode immediately.
assert.equal(vm.runInContext('mock.fastloader.enabled',context),false);
assert.equal(JSON.parse(previewStorage.get('espsette.preview.fastloader')).enabled,false);
assert.equal(vm.runInContext('source.sessionBody.get("turbo")', context), '0');
assert.equal(vm.runInContext('source.logicalSize', context), 6); // Standard ROM copies.
// A rejected immediate switch restores its saved position and preserves timing edits.
vm.runInContext('mock.motorOn=true;mock.gpioDisabled=false;render({...mock})',context);
elements.get('fastloaderEnabled').checked=true;
await elements.get('fastloaderEnabled').onchange();
assert.equal(vm.runInContext('mock.fastloader.enabled',context),false);
assert.equal(elements.get('fastloaderEnabled').checked,false);
assert.match(elements.get('error').textContent,/Cannot save fastloader switch/);
vm.runInContext('mock.motorOn=false;render({...mock})',context);
await assert.rejects(vm.runInContext("mockApi('/fastloader-config',{body:new URLSearchParams()})",context));
await assert.rejects(vm.runInContext("mockApi('/fastloader-config',{body:new URLSearchParams({holdC64Us:'32'})})",context));
// STOP is momentary; the stopped state persists on SENSE, not on a latched STOP key.
vm.runInContext('mock.motorOn=false;mock.sensePressed=true;mock.fastloaderActive=false;render({...mock})',context);
await elements.get('stop').onclick();
assert.equal(vm.runInContext('mock.sensePressed',context),false);
assert.equal(elements.get('stop').classList.values.has('pressed'),false);
assert.equal(elements.get('stop').attributes?.['aria-pressed'],undefined);
assert.equal(elements.get('play').classList.values.has('pressed'),false);
vm.runInContext('render({...mock})',context);
assert.equal(vm.runInContext('mock.sensePressed',context),false);
assert.equal(elements.get('stop').classList.values.has('pressed'),false);
await elements.get('play').onclick();
assert.equal(vm.runInContext('mock.sensePressed',context),true);
assert.equal(elements.get('play').classList.values.has('pressed'),true);
assert.equal(elements.get('stop').classList.values.has('pressed'),false);

// One-click preview works with fastloader disabled and preserves saved settings.
const savedFastloaderPreview = previewStorage.get('espsette.preview.fastloader');
vm.runInContext('mock.gpioDisabled=true;mock.motorOn=true;render({...mock})',context);
await elements.get('previewFastload').onclick();
assert.equal(vm.runInContext('source.file.name',context),'FASTLOAD-DEMO.PRG');
assert.equal(vm.runInContext('mock.fastloaderActive',context),true);
assert.equal(vm.runInContext('mock.fastloader.enabled',context),false);
assert.equal(previewStorage.get('espsette.preview.fastloader'),savedFastloaderPreview);
assert.equal(vm.runInContext('source.sessionBody.get("turbo")',context),'1');
assert.equal(vm.runInContext('mock.phase',context),'header');
vm.runInContext('advanceMockFast(.8);render({...mock})',context);
assert.equal(vm.runInContext('mock.phase',context),'fastload');
assert.ok(vm.runInContext('mock.fastloaderBytes',context)>0);
assert.equal(elements.get('fastProgress').hidden,false);
const previewHeaderCount = vm.runInContext('mock.writeHeaders',context);
elements.get('previewSaveName').onclick();
assert.equal(vm.runInContext('mock.writeHeaders',context),previewHeaderCount);
assert.match(elements.get('previewMessage').textContent,/WRITE reception is paused/);
elements.get('previewPauseReceiver').onclick();
const pausedPreviewBytes=vm.runInContext('mock.fastloaderBytes',context);
vm.runInContext('advanceMockFast(.5);render({...mock})',context);
assert.equal(vm.runInContext('mock.fastloaderBytes',context),pausedPreviewBytes);
assert.match(elements.get('fastProgressDetail').textContent,/Waiting for receiver/);
assert.equal(elements.get('previewPauseReceiver').textContent,'Resume receiver');
assert.equal(awakeUpdates.at(-1),true);
elements.get('previewPauseReceiver').onclick();
vm.runInContext('advanceMockFast(.1);render({...mock})',context);
assert.ok(vm.runInContext('mock.fastloaderBytes',context)>pausedPreviewBytes);
for(let i=0;i<50 && vm.runInContext('mock.phase',context)!=='done';i++) {
  vm.runInContext('advanceMockFast(.25)',context);
  await vm.runInContext('step()',context); // Same bounded ring/refill path as selected files.
}
assert.equal(vm.runInContext('mock.phase',context),'done');
assert.match(elements.get('fastProgressBytes').textContent,/100%/);
elements.get('previewSaveName').onclick();
assert.match(elements.get('writeDetail').textContent,/Last header: "NAZWA"/);
assert.equal(elements.get('playbackDetails').open,true);
const previousRequestId=vm.runInContext('mock.writeCommandId',context);
elements.get('previewWriteCommand').onclick();
assert.equal(vm.runInContext('mock.writeCommandId',context),previousRequestId+1);
assert.match(elements.get('writeDetail').textContent,/ESP:TEST/);
assert.equal(elements.get('writeStatus').textContent,'Request received (not executed)');
vm.runInContext('mock.motorOn=false;mock.gpioDisabled=false;',context);
// Preview settings survive reloading the page; real hardware uses LittleFS.
await vm.runInContext("mockApi('/autoplay', {body:new URLSearchParams({enabled:'0'})})", context);
const reloaded = vm.createContext({...context});
vm.runInContext(script, reloaded);
assert.equal(vm.runInContext('mock.defaultMachine', reloaded), 'c16-pal');
assert.equal(vm.runInContext('mock.autoPlay', reloaded), false);
assert.equal(vm.runInContext('mock.fastloader.enabled', reloaded), false);
assert.equal(vm.runInContext('mock.fastloader.holdC64Us', reloaded), 16);
console.log('IDX/TAP, VICE counter, English UI, session recovery and fastloader settings OK');
