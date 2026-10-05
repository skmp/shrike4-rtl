// test_engine.js -- differential test of the calculator's JS engine against the C reference (model/sh4_fpu_approx.h).
//   gcc -O2 -o /tmp/ref blog/calculator/tools/ref.c && node blog/calculator/tools/test_engine.js [iterations]
'use strict';
const { execFileSync } = require('child_process');
const E = require('../engine.js');
const N = parseInt(process.argv[2] || '400', 10);
const REF = process.env.REF || '/tmp/ref';
const hex8 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const ref = (...a) => execFileSync(REF, a.map(String)).toString().trim().split(/\s+/);

let seed = 12345;
const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
const randFloat = () => {                                   // a mix of ordinary magnitudes and awkward mantissas
  const e = 100 + Math.floor(rnd() * 56), m = Math.floor(rnd() * 0x800000), s = rnd() < 0.5 ? 1 : 0;
  return ((s << 31) | (e << 23) | m) >>> 0;
};
let fails = 0, checked = 0;
const cmp = (what, got, want) => { checked++; if (got !== want) { fails++; if (fails < 10) console.log(`  MISMATCH ${what}: js ${got} ref ${want}`); } };

for (let rz = 0; rz < 2; rz++) {
  for (let i = 0; i < N; i++) {                             // FIPR
    const a = [0, 0, 0, 0].map(randFloat), b = [0, 0, 0, 0].map(randFloat);
    const js = E.traceFIPR(a, b, !!rz);
    cmp(`fipr ${a.map(hex8)} ${b.map(hex8)} rz=${rz}`, hex8(js.bits), ref('fipr', ...a.map(hex8), ...b.map(hex8), rz)[0]);
  }
  for (let i = 0; i < Math.max(4, N >> 3); i++) {           // FTRV (xmtrx is column-major: (row,col) = m[row + 4*col])
    const m = Array.from({ length: 16 }, randFloat), v = [0, 0, 0, 0].map(randFloat);
    const rows = [0, 1, 2, 3].map((r) => [0, 1, 2, 3].map((c) => m[r + 4 * c]));
    const js = E.traceFTRV(rows, v, !!rz);
    cmp(`ftrv rz=${rz}`, js.bits.map(hex8).join(' '), ref('ftrv', ...m.map(hex8), ...v.map(hex8), rz).join(' '));
  }
  for (let i = 0; i < N; i++) {                             // FSCA: every 1 in N angles plus random ones
    const ang = i < N / 2 ? Math.floor(rnd() * 65536) : (i * 65536 / N) | 0;
    const js = E.traceFSCA(ang, !!rz);
    cmp(`fsca ${ang} rz=${rz}`, js.bits.map(hex8).join(' '), ref('fsca', ang.toString(16), rz).join(' '));
  }
  for (let i = 0; i < N; i++) {                             // FSRRA over positive normals
    const x = (randFloat() & 0x7fffffff) >>> 0;
    const js = E.traceFSRRA(x, !!rz);
    cmp(`fsrra ${hex8(x)} rz=${rz}`, hex8(js.bits), ref('fsrra', hex8(x), rz)[0]);
  }
}
// the four quadrant boundaries and the nodes, where FSCA's swap/negate flags change
for (const ang of [0, 1, 0x3ff, 0x400, 0x1000, 0x2000, 0x3fff, 0x4000, 0x8000, 0xbfff, 0xc000, 0xffff])
  for (let rz = 0; rz < 2; rz++)
    cmp(`fsca edge ${ang} rz=${rz}`, E.traceFSCA(ang, !!rz).bits.map(hex8).join(' '), ref('fsca', ang.toString(16), rz).join(' '));
// FSRRA table edges: first and last node of both parities
for (const x of [0x3f800000, 0x3fffffff, 0x40000000, 0x40ffffff, 0x00800000, 0x7f7fffff, 0x3f7fffff])
  for (let rz = 0; rz < 2; rz++)
    cmp(`fsrra edge ${hex8(x)} rz=${rz}`, hex8(E.traceFSRRA(x, !!rz).bits), ref('fsrra', hex8(x), rz)[0]);

console.log(`${checked} comparisons, ${fails} mismatches`);
process.exit(fails ? 1 : 0);
