import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseWgsl, validate, writeGlsl } from 'naga-wasm';

const source = readFileSync(process.argv[2], 'utf8');
const module = parseWgsl(source);
try {
  const info = validate(module);
  try {
    for (const [stage, entryPoint, textures] of [['vertex', 'vs_main', 2], ['fragment', 'fs_main', 1]]) {
      const glsl = writeGlsl(module, info, { version: '310 es', stage, entryPoint });
      assert(glsl.startsWith('#version 310 es'));
      assert.equal([...glsl.matchAll(/uniform highp usampler2D/g)].length, textures);
      assert(glsl.includes('texelFetch('));
      assert(!/\bbuffer\s+\w/.test(glsl));
      assert(!source.includes('ptr<storage'));
      console.log(`${stage}: WGSL validation / GLES 310 translation OK`);
    }
  } finally {
    info.free();
  }
} finally {
  module.free();
}
