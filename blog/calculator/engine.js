// engine.js -- a bit-exact JavaScript model of the SH7091 vector block (FIPR / FTRV / FSCA / FSRRA), ported from
// rtl/fpu_vec/*.sv, which is itself bit-exact against the hardware captures.  Every cycle of every instruction is
// returned as a record of what each part of the circuit held and produced, for blog/calculator/index.html.
// Verified against model/sh4_fpu_approx.h by tools/test_engine.js.  Needs tables.js (the RTL's ROMs).
'use strict';
const TB = (typeof T !== 'undefined') ? T : require('./tables.js');

// ---------------------------------------------------------------- primitives
const sx = (v, w) => BigInt.asIntN(w, v);
const bit = (v, i) => (v >> BigInt(i)) & 1n;
const K20 = 1n << 19n;                                   // the rounding constant every product register gets

// the truncated radix-4 Booth array: port B (rec, 28b signed) recoded, port A (mul, 25b signed) the multiplicand
function arr(rec, mul) {
  let acc = 0n; const rows = [];
  for (let k = 0; k < 14; k++) {
    const bm = k === 0 ? 0n : bit(rec, 2 * k - 1), b0 = bit(rec, 2 * k), b1 = bit(rec, 2 * k + 1);
    const code = (b1 << 2n) | (b0 << 1n) | bm;
    let mag;
    if (code === 0n || code === 7n) mag = 0n;
    else if (code === 3n || code === 4n) mag = mul << 1n;      // |d| = 2
    else mag = mul;                                            // |d| = 1
    const neg = b1 === 1n && !(b0 === 1n && bm === 1n);
    const row = (neg ? ~mag : mag) << BigInt(2 * k);           // negative digit: one's complement of |d|*A
    rows.push(row);
    acc += (row >> 16n) << 16n;                                // every row is cut below column 16
    if (neg && 2 * k >= 18) acc += 1n << BigInt(2 * k);        // the hot one exists only from column 18
  }
  if (bit(rows[0], 15) + bit(rows[1], 15) + bit(rows[2], 15) >= 2n) acc += 1n << 16n;   // group carry
  if ((bit(rows[6], 16) ^ bit(rows[7], 16) ^ bit(rows[8], 16)) === 1n) acc -= 1n << 16n; // group sum bit
  return { raw: acc, prod: sx((acc + K20) >> 20n, 29) };
}
const mul28 = (rec, mul) => arr(rec, mul).prod;

// one aligner: shift, optional inversion, and the hot one that every negative adder input gets
function align(p, sh, inv) {
  const shifted = sh >= 32 ? (p < 0n ? -1n : 0n) : (p >> BigInt(sh));
  const q = sx(inv ? ~shifted : shifted, 32);
  return { q, cin: q < 0n ? 1n : 0n, sh, inv };
}
const add4 = (qs) => sx(qs.reduce((a, x) => a + x.q + x.cin, 0n), 35);

// normalise, obtain positive number, round: the only rounding in any of the four instructions
function normround(sum, negate, emax, rz) {
  const s = negate ? -sum : sum, sign = s < 0n, mag = s < 0n ? -s : s;
  const d = { sum, negate, emax, rz, sign: sign ? 1 : 0, mag };
  if (mag === 0n) return { bits: 0, d: Object.assign(d, { zero: true }) };
  const top = mag.toString(2).length - 1, shamt = top - 23;
  let q24, e;
  if (shamt > 0) {
    const rem = mag & ((1n << BigInt(shamt)) - 1n), half = 1n << BigInt(shamt - 1), qw = mag >> BigInt(shamt);
    const up = !rz && (rem > half || (rem === half && (qw & 1n) === 1n));
    const q25 = qw + (up ? 1n : 0n);
    if ((q25 >> 24n) & 1n) { q24 = q25 >> 1n; e = top + 1; } else { q24 = q25; e = top; }
    Object.assign(d, { rem, half, up });
  } else { q24 = mag << BigInt(-shamt); e = top; }
  Object.assign(d, { top, shamt, q24 });
  e = e + emax - 280 + 127;
  d.e = e;
  if (e <= 0) return { bits: (sign ? 0x80000000 : 0) >>> 0, d: Object.assign(d, { under: true }) };
  if (e >= 255) return { bits: ((sign ? 0x80000000 : 0) | (rz ? 0x7f7fffff : 0x7f800000)) >>> 0, d: Object.assign(d, { over: true }) };
  return { bits: (((sign ? 1 : 0) << 31) | (e << 23) | Number(q24 & 0x7fffffn)) >>> 0, d };
}

// ---------------------------------------------------------------- float helpers
const fbuf = new DataView(new ArrayBuffer(4));
const bitsOf = (f) => { fbuf.setFloat32(0, f); return fbuf.getUint32(0); };
const floatOf = (b) => { fbuf.setUint32(0, b >>> 0); return fbuf.getFloat32(0); };
const parts = (b) => ({ s: (b >>> 31) & 1, e: (b >>> 23) & 0xff, m: b & 0x7fffff });

// ---------------------------------------------------------------- FSCA / FSRRA ROM access (rtl fpu_vec_top.sv)
const swapOf = (q, cos) => ((q & 1) ^ (cos ? 1 : 0)) === 1;
const negOf = (q, cos) => (cos ? (((q >> 1) & 1) ^ (q & 1)) : ((q >> 1) & 1)) === 1;
const romC0 = (sw, r) => BigInt(sw ? TB.FSCA_B_C0[r] : TB.FSCA_A_C0[r]);
const romL = (sw, r) => BigInt(sw ? TB.FSCA_A_L[r] : TB.FSCA_B_L[r]);
const romE = (sw, r) => (sw ? TB.FSCA_A_E[r] : TB.FSCA_B_E[r]);
const romQ = (sw, r) => BigInt(sw ? TB.FSCA_A_Q[r] : TB.FSCA_B_Q[r]);

// ---------------------------------------------------------------- cycle-record helpers
const L = (part, text) => ({ part, text });
const hx = (v, w) => (v < 0n ? '-0x' + (-v).toString(16) : '0x' + v.toString(16).padStart(w || 1, '0'));

// ---------------------------------------------------------------- FIPR and FTRV
function dotPass(av, bv, rz) {                      // one pass of the block: port A = av[i], port B = bv[i]
  const ln = av.map((ab, i) => {
    const a = parts(ab), b = parts(bv[i]);
    return {
      a, b, la: BigInt((1 << 23) | a.m), lb: BigInt((1 << 23) | b.m), E: a.e + b.e, sign: a.s ^ b.s,
      valid: a.e !== 0 && b.e !== 0 && a.e !== 255 && b.e !== 255 && a.e + b.e > 75,
    };
  });
  let nan = false, pinf = 0, ninf = 0, any = false;
  ln.forEach((l) => {
    const ia = l.a.e === 255 && l.a.m === 0, ib = l.b.e === 255 && l.b.m === 0;
    const na = l.a.e === 255 && l.a.m !== 0, nb = l.b.e === 255 && l.b.m !== 0;
    if (na || nb || (ia && l.b.e === 0) || (ib && l.a.e === 0)) nan = true;
    else if (ia || ib) { if (l.sign) ninf++; else pinf++; }
    else if (l.a.e !== 0 && l.b.e !== 0 && l.E > 75) any = true;
  });
  let special = null;
  if (nan || pinf || ninf || !any) {
    special = (nan || (pinf && ninf)) ? 0x7fbfffff
      : (pinf + ninf >= 2) ? (ln[0].sign ? 0xff800000 : 0x7f800000)
        : pinf ? 0x7f800000 : ninf ? 0xff800000 : 0x00000000;
  }
  const emax = Math.max(0, ...ln.filter((l) => l.valid).map((l) => l.E));
  ln.forEach((l) => { l.prod = l.valid ? mul28(l.lb, l.la) : 0n; l.shift = Math.min(Math.max(emax - l.E, 0), 63); });
  const qs = ln.map((l) => align(l.valid ? l.prod : 0n, l.valid ? l.shift : 0, l.valid ? !!l.sign : false));
  const sum = add4(qs);
  const nr = normround(sum, false, emax, rz);
  return { ln, emax, qs, sum, nr, special, bits: special !== null ? special >>> 0 : nr.bits };
}

function traceFIPR(fvm, fvn, rz) {
  const p = dotPass(fvm, fvn, rz), C = [];
  const lat = {}, mul = {}, m = {}, aln = {};
  p.ln.forEach((l, i) => {
    lat[i] = { a: { src: 'reg', txt: `FVm${i}`, val: l.la }, b: { src: 'reg', txt: `FVn${i}`, val: l.lb } };
    mul[i] = { op: `FVm${i} × FVn${i}`, prod: l.prod };
    m[i] = l.prod;
    aln[i] = p.qs[i];
  });
  C.push({ tag: 'RR', title: 'Register read — both vectors into the input latches',
    lat, log: p.ln.map((l, i) => L(`decode lane ${i}`,
      `A = {0,1,mant} = ${hx(l.la, 7)}   B = {1,mant} = ${hx(l.lb, 7)}   E${i} = ${l.a.e}+${l.b.e} = ${l.E}` +
      `   sign = ${l.sign}${l.valid ? '' : '   → slot forced to 0'}`))
      .concat([L('exponent logic', `Emax = max valid E[i] = ${p.emax}`)]) });
  C.push({ tag: 'F1', title: 'Multiply — four truncated Booth arrays in parallel', lat, hold: true, mul,
    log: p.ln.map((l, i) => L(`FM${i}`, `arr(B=${hx(l.lb, 7)}, A=${hx(l.la, 7)}) → M${i} = ${hx(l.prod)} (${l.prod})`)) });
  C.push({ tag: 'F2', title: 'Align to Emax and add all four at once', m, aln,
    add: { expr: 'Σ = a0b0 + a1b1 + a2b2 + a3b3', sum: p.sum },
    log: p.qs.map((q, i) => L(`align slot ${i}`, `sh = ${p.emax}−${p.ln[i].E} = ${q.sh}, inv = ${q.inv ? 1 : 0} → q = ${hx(q.q, 8)}, cin = ${q.cin}`))
      .concat([L('4-input adder', `sum = Σq + Σcin = ${hx(p.sum)} (${p.sum})`)]) });
  C.push({ tag: 'F3', title: 'Normalise and round — once', sumreg: p.sum, nrm: { emax: p.emax, negate: false },
    log: roundLog(p.nr, p.emax) });
  C.push({ tag: 'WB', title: 'Write back', resreg: p.bits, out: { label: 'FVn[3]', bits: p.bits },
    log: [L('result register', `${hx(BigInt(p.bits >>> 0), 8)} = ${floatOf(p.bits)}`)]
      .concat(p.special !== null ? [L('special path', 'operands are inf / NaN / zero: the block is bypassed')] : []) });
  return { cycles: C, bits: p.bits };
}

function traceFTRV(xm, fv, rz) {                    // xm[row][col]; port A = v[col], port B = matrix element
  const pass = [0, 1, 2, 3].map((r) => dotPass(fv, xm[r], rz));
  const latOf = (r) => Object.fromEntries([0, 1, 2, 3].map((i) => [i, {
    a: { src: 'reg', txt: `v${i}`, val: pass[r].ln[i].la }, b: { src: 'reg', txt: `m${r}${i}`, val: pass[r].ln[i].lb } }]));
  const mOf = (r) => Object.fromEntries([0, 1, 2, 3].map((i) => [i, pass[r].ln[i].prod]));
  const alnOf = (r) => Object.fromEntries([0, 1, 2, 3].map((i) => [i, pass[r].qs[i]]));
  const mulOf = (r) => Object.fromEntries([0, 1, 2, 3].map((i) => [i, { op: `v${i} × m${r}${i}`, prod: pass[r].ln[i].prod }]));
  const C = [];
  C.push({ tag: 'RR', title: 'Register read — the vector and matrix row 0', lat: latOf(0),
    log: [L('operand roles', 'port A = vector component, port B = matrix element (the matrix is the recoded operand)')]
      .concat([0, 1, 2, 3].map((i) => L(`lane ${i}`, `A = v${i} = ${hx(pass[0].ln[i].la, 7)}   B = m0${i} = ${hx(pass[0].ln[i].lb, 7)}`))) });
  for (let c = 1; c <= 7; c++) {
    const cyc = { tag: c <= 4 ? 'F1' : c <= 5 ? 'F2' : c <= 6 ? 'F3' : 'WB', log: [] };
    const mulRow = c <= 4 ? c - 1 : -1, addRow = c - 2, nrmRow = c - 3, wbRow = c - 4;
    const t = [];
    if (mulRow >= 0) {
      cyc.mul = mulOf(mulRow); if (mulRow < 3) cyc.lat = latOf(mulRow + 1);
      t.push(`row ${mulRow} multiplies`);
      [0, 1, 2, 3].forEach((i) => cyc.log.push(L(`FM${i}`, `arr(B=m${mulRow}${i}, A=v${i}) → M${i} = ${hx(pass[mulRow].ln[i].prod)}`)));
    }
    if (addRow >= 0 && addRow < 4) {
      cyc.m = mOf(addRow); cyc.aln = alnOf(addRow);
      cyc.add = { expr: `row ${addRow}:  Σ m${addRow}ⱼ·vⱼ`, sum: pass[addRow].sum };
      t.push(`row ${addRow} is added`);
      pass[addRow].qs.forEach((q, i) => cyc.log.push(L(`align slot ${i}`, `sh = ${q.sh}, inv = ${q.inv ? 1 : 0} → q = ${hx(q.q, 8)}, cin = ${q.cin}`)));
      cyc.log.push(L('4-input adder', `row ${addRow} sum = ${hx(pass[addRow].sum)} (${pass[addRow].sum})`));
    }
    if (nrmRow >= 0 && nrmRow < 4) {
      cyc.sumreg = pass[nrmRow].sum; cyc.nrm = { emax: pass[nrmRow].emax, negate: false };
      t.push(`row ${nrmRow} is rounded`);
      cyc.log.push(...roundLog(pass[nrmRow].nr, pass[nrmRow].emax, `row ${nrmRow}: `));
    }
    if (wbRow >= 0 && wbRow < 4) {
      cyc.resreg = pass[wbRow].bits; cyc.out = { label: `FV[${wbRow}]`, bits: pass[wbRow].bits };
      t.push(`FV[${wbRow}] is written`);
      cyc.log.push(L('result register', `FV[${wbRow}] = ${hx(BigInt(pass[wbRow].bits >>> 0), 8)} = ${floatOf(pass[wbRow].bits)}`));
    }
    cyc.title = t.join(', ');
    C.push(cyc);
  }
  return { cycles: C, bits: pass.map((p) => p.bits) };
}

function roundLog(nr, emax, pre) {
  const d = nr.d, p = pre || '';
  const out = [L('normalise', `${p}sum = ${hx(d.sum)}${d.negate ? ' → negate' : ''}, |s| = ${hx(d.mag)}, emax = ${emax}`)];
  if (d.zero) { out.push(L('round', `${p}sum is zero → +0`)); return out; }
  out.push(L('round', `${p}top = ${d.top}, q24 = ${hx(d.q24, 6)}` +
    (d.rem !== undefined ? `, rem = ${hx(d.rem)} vs half = ${hx(d.half)} → ${d.up ? 'round up' : 'keep'}` : ', exact')));
  out.push(L('exponent', `${p}e = top + emax − 280 + 127 = ${d.top} + ${emax} − 153 = ${d.e}` +
    (d.under ? ' → underflow, ±0' : d.over ? ' → overflow' : '')));
  return out;
}

// ---------------------------------------------------------------- FSCA
function traceFSCA(fpul, rz) {
  fpul &= 0xffff;
  const node = ((fpul >> 10) + ((fpul >> 9) & 1)) & 0x3f, r = node & 15, q = (node >> 4) & 3;
  const t = BigInt.asIntN(10, BigInt(fpul & 0x3ff)), dx = sx(t << 13n, 24);
  const sw0 = swapOf(q, false), sw1 = swapOf(q, true);                    // sine word set / cosine word set
  const KC = BigInt(TB.FSCA_KC);
  const laDX = sx(dx, 25), lbDX = sx(dx, 28);
  const P = {};                                                          // product registers per step
  // step 1
  const s1 = { 0: arr(lbDX, laDX), 2: arr(sx(KC, 28), laDX), 3: arr(sx(romL(sw0, r), 28), laDX) };
  // step 2: feedback of M0 and M2, shifted right by three
  const fb = (p) => p >> 3n;
  const s2 = { 0: arr(sx(fb(s1[0].prod), 28), laDX), 2: arr(lbDX, sx(fb(s1[2].prod), 25)) };
  // step 3
  const qs5 = (sw) => sx(romQ(sw, r) << 5n, 28);
  const s3 = { 0: arr(qs5(sw0), sx(fb(s2[0].prod), 25)), 2: arr(sx(romL(!sw0, r), 28), sx(fb(s2[2].prod), 25)) };
  // step 4: only port B reloads; port A holds A3 (= M0>>3 of step 2) and J (= M2>>3 of step 2), dx stays on FM3
  const s4 = { 0: arr(qs5(sw1), sx(fb(s2[0].prod), 25)), 2: arr(sx(romL(!sw1, r), 28), sx(fb(s2[2].prod), 25)),
               3: arr(sx(romL(sw1, r), 28), laDX) };
  const slots = (sw, M0, M2, M3) => [align(M0, 6, !sw), { q: sx(romC0(sw, r), 32), cin: 0n, sh: 0, inv: false, c0: true },
    align(M2, 7 + romE(!sw, r), true), align(M3, 3 + romE(sw, r), sw)];
  const sinQ = slots(sw0, s3[0].prod, s3[2].prod, s1[3].prod), sinSum = add4(sinQ);
  const cosQ = slots(sw1, s4[0].prod, s4[2].prod, s4[3].prod), cosSum = add4(cosQ);
  const sinNR = normround(sinSum, negOf(q, false), 254, rz), cosNR = normround(cosSum, negOf(q, true), 254, rz);
  const lat = (a, b) => ({ a, b });
  const V = (src, txt, val) => ({ src, txt, val });
  const C = [];
  C.push({ tag: 'RR', title: 'Decode the angle, read both ROM words, select the first operands',
    lat: { 0: lat(V('dx', 'dx', laDX), V('dx', 'dx', lbDX)), 2: lat(V('dx', 'dx', laDX), V('rom', 'KC', KC)),
           3: lat(V('dx', 'dx', laDX), V('rom', 'L(sin)', romL(sw0, r))) },
    src: ['dec', 'rom'],
    log: [L('decode', `node = (FPUL[15:10] + FPUL[9]) & 63 = ${node}  →  r = ${r}, quadrant q = ${q}`),
      L('decode', `t = signed FPUL[9:0] = ${t},  dx = t << 13 = ${hx(dx)} (${dx})`),
      L('decode', `swap(sine) = ${sw0 ? 1 : 0}, swap(cosine) = ${sw1 ? 1 : 0}, negate(sine) = ${negOf(q, false) ? 1 : 0}, negate(cosine) = ${negOf(q, true) ? 1 : 0}`),
      L('ROM word', `C0 = ${hx(romC0(sw0, r))}, L(sin) = ${hx(romL(sw0, r))}, E(sin) = ${romE(sw0, r)}, Q(sin) = ${hx(romQ(sw0, r))}`),
      L('ROM word', `L(cos) = ${hx(romL(!sw0, r))}, E(cos) = ${romE(!sw0, r)}, Q(cos) = ${hx(romQ(sw1, r))}, KC = ${hx(KC)}`)] });
  C.push({ tag: 'F1', title: 'Step 1 — square, KC·dx, and the sine\'s linear term', hold: true,
    lat: { 0: lat(V('dx', 'dx', laDX), V('dx', 'dx', lbDX)), 2: lat(V('dx', 'dx', laDX), V('rom', 'KC', KC)),
           3: lat(V('dx', 'dx', laDX), V('rom', 'L(sin)', romL(sw0, r))) },
    mul: { 0: { op: 'dx × dx', prod: s1[0].prod }, 2: { op: 'dx × KC', prod: s1[2].prod }, 3: { op: 'dx × L(sin)', prod: s1[3].prod } },
    log: [L('FM0', `arr(dx, dx) → M0 = ${hx(s1[0].prod)} (= 64t²)`), L('FM2', `arr(KC, dx) → M2 = ${hx(s1[2].prod)} (= KC·dx)`),
      L('FM3', `arr(L(sin), dx) → M3 = ${hx(s1[3].prod)} (= T1, the linear term)`), L('FM1', 'idle — only FIPR and FTRV use it')] });
  C.push({ tag: 'F1', title: 'Step 2 — feedback: M0»3 builds the cube, M2»3 becomes J',
    m: { 0: s1[0].prod, 2: s1[2].prod, 3: s1[3].prod }, fb: [0, 2],
    lat: { 0: lat(V('hold', 'dx', laDX), V('fb0', 'M0»3', fb(s1[0].prod))), 2: lat(V('fb2', 'M2»3', fb(s1[2].prod)), V('dx', 'dx', lbDX)) },
    mul: { 0: { op: 'dx × (M0»3)', prod: s2[0].prod }, 2: { op: '(M2»3) × dx', prod: s2[2].prod } },
    log: [L('feedback', `M0 >>> 3 = ${hx(fb(s1[0].prod))} (8t²) → FM0 port B;  M2 >>> 3 = ${hx(fb(s1[2].prod))} (I1) → FM2 port A`),
      L('FM0', `arr(M0»3, dx) → M0 = ${hx(s2[0].prod)} (= 8t³ before the shift, A3)`),
      L('FM2', `arr(dx, M2»3) → M2 = ${hx(s2[2].prod)} (= J)`)] });
  C.push({ tag: 'F1', title: 'Step 3 — the cubic and the even term of the sine',
    m: { 0: s2[0].prod, 2: s2[2].prod, 3: s1[3].prod }, fb: [0, 2],
    lat: { 0: lat(V('fb0', 'M0»3', fb(s2[0].prod)), V('rom', 'Q(sin)«5', romQ(sw0, r) << 5n)),
           2: lat(V('fb2', 'M2»3', fb(s2[2].prod)), V('rom', 'L(cos)', romL(!sw0, r))) },
    mul: { 0: { op: '(M0»3) × Q(sin)«5', prod: s3[0].prod }, 2: { op: '(M2»3) × L(cos)', prod: s3[2].prod } },
    log: [L('FM0', `arr(Q(sin)«5, A3=${hx(fb(s2[0].prod))}) → M0 = ${hx(s3[0].prod)} (= T3, cubic)`),
      L('FM2', `arr(L(cos), J=${hx(fb(s2[2].prod))}) → M2 = ${hx(s3[2].prod)} (= T2, even term — the complementary word's LINEAR field)`)] });
  C.push({ tag: 'F1', title: 'Step 4 — the cosine multiplies; the sine is aligned and added',
    m: { 0: s3[0].prod, 2: s3[2].prod, 3: s1[3].prod }, c0: true,
    lat: { 0: lat(V('hold', 'A3', fb(s2[0].prod)), V('rom', 'Q(cos)«5', romQ(sw1, r) << 5n)),
           2: lat(V('hold', 'J', fb(s2[2].prod)), V('rom', 'L(sin)', romL(!sw1, r))),
           3: lat(V('hold', 'dx', laDX), V('rom', 'L(cos)', romL(sw1, r))) },
    mul: { 0: { op: '(A3) × Q(cos)«5', prod: s4[0].prod }, 2: { op: '(J) × L(sin)', prod: s4[2].prod }, 3: { op: 'dx × L(cos)', prod: s4[3].prod } },
    aln: { 0: sinQ[0], 1: sinQ[1], 2: sinQ[2], 3: sinQ[3] },
    add: { expr: 'sine:  C0 + T1 − T2 − T3', sum: sinSum },
    log: [L('align slot 0', `T3: sh = 6, inv = ${sinQ[0].inv ? 1 : 0} → q = ${hx(sinQ[0].q, 8)}, cin = ${sinQ[0].cin}`),
      L('align slot 1', `C0 = ${hx(romC0(sw0, r))} enters unshifted, no hot one`),
      L('align slot 2', `T2: sh = 7 + E(cos) = ${sinQ[2].sh}, inv = 1 → q = ${hx(sinQ[2].q, 8)}, cin = ${sinQ[2].cin}`),
      L('align slot 3', `T1: sh = 3 + E(sin) = ${sinQ[3].sh}, inv = ${sinQ[3].inv ? 1 : 0} → q = ${hx(sinQ[3].q, 8)}, cin = ${sinQ[3].cin}`),
      L('4-input adder', `sine sum = ${hx(sinSum)} (${sinSum}) on the 2⁻²⁶ grid`),
      L('FM0/FM2/FM3', `cosine products: T3c = ${hx(s4[0].prod)}, T2c = ${hx(s4[2].prod)}, T1c = ${hx(s4[3].prod)}`)] });
  C.push({ tag: 'F2', title: 'The sine is rounded while the cosine is added',
    m: { 0: s4[0].prod, 2: s4[2].prod, 3: s4[3].prod }, c0: true,
    aln: { 0: cosQ[0], 1: cosQ[1], 2: cosQ[2], 3: cosQ[3] },
    add: { expr: 'cosine:  C0 + T1 − T2 − T3', sum: cosSum },
    sumreg: sinSum, nrm: { emax: 254, negate: negOf(q, false) },
    log: [L('align slots', `cosine: sh = 6 / 0 / ${cosQ[2].sh} / ${cosQ[3].sh}, inv = ${cosQ[0].inv ? 1 : 0}/0/1/${cosQ[3].inv ? 1 : 0}`),
      L('4-input adder', `cosine sum = ${hx(cosSum)} (${cosSum})`)].concat(roundLog(sinNR, 254, 'sine: ')) });
  C.push({ tag: 'F3', title: 'FR[n] = sin is written; the cosine is rounded',
    sumreg: cosSum, nrm: { emax: 254, negate: negOf(q, true) }, resreg: sinNR.bits, out: { label: 'FR[n] = sin', bits: sinNR.bits },
    log: [L('result register', `sin = ${hx(BigInt(sinNR.bits >>> 0), 8)} = ${floatOf(sinNR.bits)}`)].concat(roundLog(cosNR, 254, 'cosine: ')) });
  C.push({ tag: 'WB', title: 'FR[n+1] = cos is written', resreg: cosNR.bits, out: { label: 'FR[n+1] = cos', bits: cosNR.bits },
    log: [L('result register', `cos = ${hx(BigInt(cosNR.bits >>> 0), 8)} = ${floatOf(cosNR.bits)}`)] });
  return { cycles: C, bits: [sinNR.bits, cosNR.bits] };
}

// ---------------------------------------------------------------- FSRRA
function traceFSRRA(x, rz) {
  const p = parts(x >>> 0);
  let special = null;
  if (p.e === 0xff) special = (p.m !== 0 || p.s) ? 0x7fbfffff : 0x00000000;
  else if (p.e === 0) special = (p.s ? 0xff800000 : 0x7f800000);
  else if (p.s) special = 0x7fbfffff;
  const e = p.e - 127, par = e & 1, k = e >> 1;
  const ent = ((p.m + (1 << 18)) >> 19) + (par ? 17 : 0);
  const t = BigInt.asIntN(19, BigInt(p.m & 0x7ffff)), dx = sx(t << 4n, 24);
  const laDX = sx(dx, 25), lbDX = sx(dx, 28), emax = 254 - k;
  const REC1 = BigInt(TB.FSRRA_REC1[ent]), REC2 = BigInt(TB.FSRRA_REC2[ent]) << 9n, REC3 = BigInt(TB.FSRRA_REC3[ent]) << 8n;
  const C0 = BigInt(TB.FSRRA_C0[ent]), E1 = TB.FSRRA_E1[ent];
  const s1 = { 0: arr(lbDX, laDX), 3: arr(sx(REC1, 28), laDX) };
  const fb = (v) => v >> 3n;
  const sq = fb(s1[0].prod);
  const s2 = { 0: arr(sx(REC3, 28), sx(sq, 25)), 2: arr(sx(REC2, 28), sx(sq, 25)) };
  const s3 = { 0: arr(lbDX, sx(fb(s2[0].prod), 25)) };
  const qs = [align(s3[0].prod, 7, true), { q: sx(C0, 32), cin: 0n, sh: 0, inv: false, c0: true },
    align(s2[2].prod, 9, false), align(s1[3].prod, 6 - E1, true)];
  const sum = add4(qs), nr = normround(sum, false, emax, rz);
  const bits = special !== null ? special >>> 0 : nr.bits;
  const V = (src, txt, val) => ({ src, txt, val });
  const lat = (a, b) => ({ a, b });
  const C = [];
  C.push({ tag: 'RR', title: 'Decode the operand, address the ROM, select the first operands', src: ['regs', 'dec', 'rom'],
    lat: { 0: lat(V('dx', 'dx', laDX), V('dx', 'dx', lbDX)), 3: lat(V('dx', 'dx', laDX), V('rom', 'REC1', REC1)) },
    log: [L('decode', `exp = ${p.e} → e = ${e}, parity = ${par}, k = ${k}, emax = 254 − k = ${emax}`),
      L('decode', `node = (mant + 2¹⁸) >> 19 = ${(p.m + (1 << 18)) >> 19} → ROM entry ${ent}`),
      L('decode', `t = signed mant[18:0] = ${t}, dx = t << 4 = ${hx(dx)} (${dx})`),
      L('ROM word', `C0 = ${hx(C0)}, REC1 = ${hx(REC1)} (E1 = ${E1}), REC2 = ${hx(BigInt(TB.FSRRA_REC2[ent]))}, REC3 = ${hx(BigInt(TB.FSRRA_REC3[ent]))}`)]
      .concat(special !== null ? [L('special path', `operand is zero / denormal / negative / inf / NaN → ${hx(BigInt(special >>> 0), 8)}, the block is bypassed`)] : []) });
  C.push({ tag: 'F1', title: 'Step 1 — the square and the linear term', hold: true,
    lat: { 0: lat(V('dx', 'dx', laDX), V('dx', 'dx', lbDX)), 3: lat(V('dx', 'dx', laDX), V('rom', 'REC1', REC1)) },
    mul: { 0: { op: 'dx × dx', prod: s1[0].prod }, 3: { op: 'dx × REC1', prod: s1[3].prod } },
    log: [L('FM0', `arr(dx, dx) → M0 = ${hx(s1[0].prod)} (= the square)`), L('FM3', `arr(REC1, dx) → M3 = ${hx(s1[3].prod)} (= T1)`)] });
  C.push({ tag: 'F1', title: 'Step 2 — both chains read the same square latch, shifted right by three',
    m: { 0: s1[0].prod, 3: s1[3].prod }, fb: [0],
    lat: { 0: lat(V('fb0', 'M0»3', sq), V('rom', 'REC3«8', REC3)), 2: lat(V('fb0', 'M0»3', sq), V('rom', 'REC2«9', REC2)) },
    mul: { 0: { op: '(M0»3) × REC3«8', prod: s2[0].prod }, 2: { op: '(M0»3) × REC2«9', prod: s2[2].prod } },
    log: [L('feedback', `M0 >>> 3 = ${hx(sq)} goes to BOTH FM0 and FM2, on the multiplicand port`),
      L('FM2', `arr(REC2«9, sq) → M2 = ${hx(s2[2].prod)} (= T2, square term; REC2 is fixed-point)`),
      L('FM0', `arr(REC3«8, sq) → M0 = ${hx(s2[0].prod)} (= I, start of the cubic)`)] });
  C.push({ tag: 'F1', title: 'Step 3 — the cubic term', m: { 0: s2[0].prod, 2: s2[2].prod, 3: s1[3].prod }, fb: [0],
    lat: { 0: lat(V('fb0', 'M0»3', fb(s2[0].prod)), V('dx', 'dx', lbDX)) },
    mul: { 0: { op: '(M0»3) × dx', prod: s3[0].prod } },
    log: [L('FM0', `arr(dx, I=${hx(fb(s2[0].prod))}) → M0 = ${hx(s3[0].prod)} (= T3; dx is the recoded operand here)`)] });
  C.push({ tag: 'F2', title: 'Align and add the four terms', m: { 0: s3[0].prod, 2: s2[2].prod, 3: s1[3].prod }, c0: true,
    aln: { 0: qs[0], 1: qs[1], 2: qs[2], 3: qs[3] }, add: { expr: 'C0 − T1 + T2 − T3', sum },
    log: [L('align slot 0', `T3: sh = 7, inv = 1 → q = ${hx(qs[0].q, 8)}, cin = ${qs[0].cin}`),
      L('align slot 1', `C0 = ${hx(C0)} enters unshifted, no hot one`),
      L('align slot 2', `T2: sh = 9, inv = 0 → q = ${hx(qs[2].q, 8)}, cin = ${qs[2].cin}`),
      L('align slot 3', `T1: sh = 6 − E1 = ${qs[3].sh}, inv = 1 → q = ${hx(qs[3].q, 8)}, cin = ${qs[3].cin}`),
      L('4-input adder', `sum = ${hx(sum)} (${sum})`)] });
  C.push({ tag: 'F3', title: 'Normalise and round', sumreg: sum, nrm: { emax, negate: false }, log: roundLog(nr, emax) });
  C.push({ tag: 'WB', title: 'Write back', resreg: bits, out: { label: 'FRn = 1/√x', bits },
    log: [L('result register', `${hx(BigInt(bits >>> 0), 8)} = ${floatOf(bits)}`)] });
  return { cycles: C, bits };
}

const API = { arr, mul28, align, add4, normround, bitsOf, floatOf, parts, hx, traceFIPR, traceFTRV, traceFSCA, traceFSRRA };
if (typeof module !== 'undefined') module.exports = API;
