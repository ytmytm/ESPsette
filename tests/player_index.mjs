import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/player.html', import.meta.url), 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];
const elements = new Map();
function element() {
  return {
    files: [], paused: true, hidden: false, textContent: '',
    classList: {add(){}, remove(){}, toggle(){}},
    replaceChildren(){this.children = []}, append(child){(this.children ||= []).push(child)},
    play(){this.paused = false; return Promise.resolve()},
    pause(){this.paused = true}, removeAttribute(){}, load(){}
  };
}
const context = vm.createContext({
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
  setTimeout() {} // The initial mock UI tick runs once; no background loop.
});
vm.runInContext(script, context);

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

for (const payload of [[0, 0x10, 0], [0, 0, 0, 0]]) {
  context.badTape = new Blob([new Uint8Array(20), Uint8Array.from(payload)]);
  await assert.rejects(vm.runInContext('scanTape(badTape)', context),
    /Ucięty impuls TAP|Zerowy impuls TAP/);
}
context.badTape = new Blob([new Uint8Array(20), Uint8Array.of(0x2f)]);
context.badHeader = new Uint8Array(20);
context.badHeader.set(new TextEncoder().encode('C64-TAPE-RAW'));
context.badHeader[12] = 1;
context.badHeader[16] = 2; // Declares two data bytes, Blob has one.
assert.throws(() => vm.runInContext('tapFormat(badHeader, badTape.size)', context),
  /Długość danych TAP/);

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
      offset:'${offset}', cycles:'100', phase:'0'})})`, context), /Błędny offset TAP/);
  assert.equal(vm.runInContext('mock.sessionId', context), jumped.sessionId);
}
await assert.rejects(vm.runInContext(
  `mockApi('/chunk?sid=${created.sessionId}&offset=25', {body:new FormData()})`, context
), /offset/);

await assert.rejects(
  vm.runInContext('parseIdx(bad)', vm.createContext({...context,
    bad:{size:10, text:async()=> '0xGG Unsupported'}})),
  /Nieznany format IDX/
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
const resume = await vm.runInContext(
  `mockApi('/session', {body:new URLSearchParams({type:'tap', size:'26',
    name:'resume.tap', cycles:'920'})})`, context
);
context.resumeTape = tape;
vm.runInContext(`source = {file:resumeTape, type:'tap', id:${resume.sessionId},
  offset:20, logicalSize:26, total:920, indexEntries:[]};
  originalApi = api; api = async () => { throw Error('offline'); };`, context);
await vm.runInContext('step()', context);
assert.equal(vm.runInContext('source.offset', context), 20);
assert.match(elements.get('transport').textContent, /Brak połączenia/);
vm.runInContext('api = originalApi; mock.fileOffsetReceived = 21; mockBytes = 1;', context);
await vm.runInContext('step()', context);
assert.equal(vm.runInContext('source.offset', context), 26);
assert.equal(vm.runInContext('mock.fileOffsetReceived', context), 26);
assert.match(elements.get('transport').textContent, /Połączenie wznowione/);
vm.runInContext('mock.sessionId++', context);
await vm.runInContext('step()', context);
assert.equal(vm.runInContext('source', context), null);
assert.match(elements.get('transport').textContent, /Brak pliku źródłowego/);
vm.runInContext("document.getElementById('saveMock').onclick()", context);
assert.match(elements.get('writeDetail').textContent, /ESP:TEST/);
vm.runInContext("mock.writeMachine = 'c16'; render({...mock})", context);
assert.match(elements.get('writeDetail').textContent, /C16\/Plus4/);
console.log('IDX/TAP errors, session recovery, C64/C16 PRG and WRITE preview OK');
