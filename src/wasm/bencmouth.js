// BENCmouth - JavaScript wrapper for the WebAssembly build.
//
// The module is bare wasm32 with no libc and no generated runtime, so this is
// the entire glue layer. It works unchanged in a browser and in Node.
//
//   const bm = await BENCmouth.load('bencmouth.wasm');
//   const pcm = bm.say('hello world');          // Float32Array
//   console.log(bm.phonemes('hello world'));    // 'HH EH L OW W ER L D'

const BENCmouth = (() => {
  const decoder = new TextDecoder();
  const encoder = new TextEncoder();

  class Instance {
    constructor(exports) {
      this.x = exports;
      this.mem = () => new Uint8Array(this.x.memory.buffer);
      if (!this.x.bm_wasm_init(22050)) throw new Error('BENCmouth failed to initialise');
      this.sampleRate = this.x.bm_wasm_sample_rate();
    }

    // Writes a JS string into the module's text buffer and returns its length
    // in bytes. Everything crossing the boundary goes through here.
    _writeText(text) {
      const bytes = encoder.encode(text);
      const cap = this.x.bm_wasm_text_capacity();
      if (bytes.length > cap) {
        throw new Error(`text is ${bytes.length} bytes, buffer holds ${cap}`);
      }
      this.mem().set(bytes, this.x.bm_wasm_text_buffer());
      return bytes.length;
    }

    _readText(len) {
      const at = this.x.bm_wasm_text_buffer();
      return decoder.decode(this.mem().subarray(at, at + len));
    }

    voice(name) {
      if (!this.x.bm_wasm_set_voice(this._cstr(name))) {
        throw new Error(`unknown voice: ${name}`);
      }
      return this;
    }

    // Voice parameters by name, matching the .voice file keys.
    param(key, value) {
      if (!this.x.bm_wasm_set_param(this._cstr(key), value)) {
        throw new Error(`unknown voice parameter: ${key}`);
      }
      return this;
    }

    // The effects chain, which is deliberately not part of the voice: any
    // effect composes with any speaker. See bm_effects in bencmouth.h.
    effects(name) {
      if (!this.x.bm_wasm_set_effects(this._cstr(name))) {
        throw new Error(`unknown effects preset: ${name}`);
      }
      return this;
    }

    fx(key, value) {
      if (!this.x.bm_wasm_set_fx_param(this._cstr(key), value)) {
        throw new Error(`unknown effect parameter: ${key}`);
      }
      return this;
    }

    markup(on) { this.x.bm_wasm_set_markup(on ? 1 : 0); return this; }

    // Borrows the text buffer for a NUL-terminated string argument. Safe only
    // because the calls that take one do not also read queued text.
    _cstr(s) {
      const bytes = encoder.encode(s);
      const cap = this.x.bm_wasm_text_capacity();
      // The terminator needs a byte of its own, which _writeText does not
      // account for - it is sizing a counted string, not a C one.
      if (bytes.length + 1 > cap) throw new Error('string too long');
      const at = this.x.bm_wasm_text_buffer();
      const mem = this.mem();
      mem.set(bytes, at);
      mem[at + bytes.length] = 0;
      return at;
    }

    phonemes(text) {
      const n = this._writeText(text);
      const out = this.x.bm_wasm_to_phonemes(n);
      if (out < 0) throw new Error('could not convert that text');
      return this._readText(out);
    }

    // Renders to a single Float32Array. Pulls in chunks because that is what
    // the engine's interface is - it never knows the total length in advance,
    // and neither do we.
    _render(queued) {
      if (!queued) throw new Error('nothing to say');
      const cap = this.x.bm_wasm_output_capacity();
      const at = this.x.bm_wasm_output_buffer();
      const chunks = [];
      let total = 0;

      while (this.x.bm_wasm_speaking()) {
        const got = this.x.bm_wasm_read(cap);
        if (got === 0) break;
        // A fresh view each time: the buffer can be detached if memory grows.
        const view = new Float32Array(this.x.memory.buffer, at, got);
        chunks.push(view.slice());
        total += got;
      }

      const pcm = new Float32Array(total);
      let off = 0;
      for (const c of chunks) { pcm.set(c, off); off += c.length; }
      return pcm;
    }

    say(text) {
      const n = this._writeText(text);
      return this._render(this.x.bm_wasm_speak(n));
    }

    sayPhonemes(phonemes) {
      const n = this._writeText(phonemes);
      return this._render(this.x.bm_wasm_speak_phonemes(n));
    }

    // Convenience for the browser: play through Web Audio.
    async play(text, ctx) {
      const pcm = this.say(text);
      const audio = ctx || new (globalThis.AudioContext || globalThis.webkitAudioContext)();
      const buf = audio.createBuffer(1, pcm.length, this.sampleRate);
      buf.getChannelData(0).set(pcm);
      const src = audio.createBufferSource();
      src.buffer = buf;
      src.connect(audio.destination);
      src.start();
      return new Promise(done => { src.onended = done; });
    }
  }

  async function load(source) {
    let bytes;
    if (source instanceof Uint8Array || source instanceof ArrayBuffer) {
      bytes = source;
    } else if (typeof process !== 'undefined' && process.versions?.node) {
      const { readFile } = await import('node:fs/promises');
      bytes = await readFile(source);
    } else {
      bytes = await (await fetch(source)).arrayBuffer();
    }
    const { instance } = await WebAssembly.instantiate(
      bytes instanceof Uint8Array ? bytes.buffer ?? bytes : bytes, {});
    return new Instance(instance.exports);
  }

  return { load };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = BENCmouth;
export default BENCmouth;
export { BENCmouth };
