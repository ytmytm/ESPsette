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

// One short pulse at 0x14, a four-byte long pulse at 0x15, then short at 0x19.
const bytes = new Uint8Array(20 + 1 + 4 + 1);
bytes.set([0x2f, 0, 0x10, 0, 0, 0x42], 20);
const tape = new Blob([bytes]);
const scanContext = vm.createContext({...context, tape});
const scan = await vm.runInContext('scanTape(tape, [20,21,22,25])', scanContext);
assert.equal(scan.total, 47 * 8 + 16 + 66 * 8);
assert.deepEqual(Array.from(scan.positions.keys()), [20, 21, 25]);
assert.equal(scan.positions.get(25), 47 * 8 + 16);

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
await assert.rejects(vm.runInContext(
  `mockApi('/chunk?sid=${created.sessionId}&offset=25', {body:new FormData()})`, context
), /offset/);

await assert.rejects(
  vm.runInContext('parseIdx(bad)', vm.createContext({...context,
    bad:{size:10, text:async()=> '0xGG Unsupported'}})),
  /Nieznany format IDX/
);
console.log('IDX parser and TAP boundary checks OK');
