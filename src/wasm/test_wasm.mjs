// Smoke test for the WebAssembly build. Run: node src/wasm/test_wasm.mjs bencmouth.wasm
import BENCmouth from './bencmouth.js';

const path = process.argv[2] || 'bencmouth.wasm';
let failures = 0;
const check = (ok, what) => {
  console.log(`  ${what.padEnd(56)} ${ok ? 'ok' : 'FAIL'}`);
  if (!ok) failures++;
};

const bm = await BENCmouth.load(path);
console.log(`\nBENCmouth wasm smoke test`);
console.log(`  sample rate ${bm.sampleRate} Hz, engine state ${bm.x.bm_wasm_engine_size()} bytes\n`);

const ph = bm.phonemes('hello world');
console.log(`  phonemes: ${ph}`);
check(ph.includes('L OW'), 'text converts to phonemes');

const pcm = bm.say('The quick brown fox jumps over the lazy dog.');
const peak = pcm.reduce((m, x) => Math.max(m, Math.abs(x)), 0);
const finite = pcm.every(Number.isFinite);
console.log(`  rendered ${pcm.length} samples (${(pcm.length / bm.sampleRate).toFixed(2)} s), peak ${peak.toFixed(3)}`);
check(pcm.length > bm.sampleRate, 'renders more than a second of audio');
check(finite, 'every sample is finite');
check(peak > 0.05 && peak < 1.5, 'peak level is sane');

bm.voice('retro');
const retro = bm.say('hello');
check(retro.length > 0, 'voice presets resolve');

bm.param('f0_base', 90);
const low = bm.say('hello');
check(low.length > 0, 'voice parameters apply');

let threw = false;
try { bm.voice('nonexistent'); } catch { threw = true; }
check(threw, 'an unknown voice is an error, not a silent default');

bm.markup(true);
const marked = bm.phonemes('one [pause 500] two');
console.log(`  markup:   ${marked}`);
check(marked.includes('[pause 500]'), 'inline markup survives to the phoneme stream');

console.log(`\n${failures ? 'FAILED' : 'all passed'} (${failures} failure${failures === 1 ? '' : 's'})\n`);
process.exit(failures ? 1 : 0);
