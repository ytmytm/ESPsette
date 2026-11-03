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
    classList: {add(){}, remove(){}, toggle(){}},
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
const context = vm.createContext({
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
assert.equal(vm.runInContext('mock.defaultMachine', context), 'c16');
assert.equal(previewStorage.get('espsette.preview.machine'), 'c16');
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
vm.runInContext("document.getElementById('saveMock').onclick()", context);
assert.match(elements.get('writeDetail').textContent, /ESP:TEST/);
vm.runInContext("mock.writeMachine = 'c16'; render({...mock})", context);
assert.match(elements.get('writeDetail').textContent, /C16\/Plus4/);
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
assert.equal(elements.get('played').textContent, '0:00 / 2:00');
vm.runInContext('mock.playedCycles=40*mock.clockHz-1; render({...mock})', context);
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[1].offset', context)));
vm.runInContext('mock.playedCycles=40*mock.clockHz; render({...mock})', context);
assert.equal(elements.get('loaded').textContent, 'Third section');
assert.equal(elements.get('played').textContent, '0:40 / 2:00');
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[2].offset', context)));
vm.runInContext('mock.playedCycles=119*mock.clockHz; render({...mock})', context);
assert.equal(elements.get('fastForward').disabled, true);
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[5].offset', context)));
await vm.runInContext("document.getElementById('rewind').onclick()", context);
assert.equal(vm.runInContext('source.seekCycles', context), 80*985248);
assert.equal(elements.get('loaded').textContent, 'Fifth section');
assert.equal(elements.get('played').textContent, '1:20 / 2:00');
// While choosing a Jump destination, polling must not overwrite the choice.
context.document.activeElement=elements.get('idxPositions');
vm.runInContext("document.getElementById('idxPositions').value=String(source.indexEntries[1].offset); document.getElementById('idxPositions').onchange(); render({...mock})", context);
assert.equal(elements.get('idxPositions').value,
  String(vm.runInContext('source.indexEntries[1].offset', context)));
context.document.activeElement=elements.get('seek');
await vm.runInContext("document.getElementById('seek').onclick()", context);
assert.equal(vm.runInContext('source.seekCycles', context), 20*985248);
assert.equal(elements.get('loaded').textContent, 'Second section');
assert.equal(elements.get('played').textContent, '0:20 / 2:00');
assert.equal(elements.get('remaining').textContent, '1:40');
vm.runInContext('mock.playedCycles=20*mock.clockHz; render({...mock})', context);
assert.equal(elements.get('loaded').textContent, 'Third section');
assert.equal(elements.get('played').textContent, '0:40 / 2:00');
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
// Preview settings survive reloading the page; real hardware uses LittleFS.
await vm.runInContext("mockApi('/autoplay', {body:new URLSearchParams({enabled:'0'})})", context);
const reloaded = vm.createContext({...context});
vm.runInContext(script, reloaded);
assert.equal(vm.runInContext('mock.defaultMachine', reloaded), 'c16');
assert.equal(vm.runInContext('mock.autoPlay', reloaded), false);
console.log('IDX/TAP, VICE counter, English UI, session recovery and WRITE preview OK');
