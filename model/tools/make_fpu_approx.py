#!/usr/bin/env python3
"""make_fpu_approx.py -- build model/sh4_fpu_approx.h from the three per-instruction headers, so the combined header can
never drift from them: the instruction code is copied VERBATIM, only the three private copies of the multiplier array,
the aligner and the portable shift are replaced by ONE shared core.   usage: python3 tools/make_fpu_approx.py  (from model/)"""
import re, sys, os
here = os.path.dirname(os.path.abspath(__file__)); model = os.path.dirname(here)
SRC = [("fipr", "fipr/sh4_fipr.h", "FIPR / FTRV"), ("fsca", "fsca/sh4_fsca.h", "FSCA"), ("fsrra", "fsrra/sh4_fsrra.h", "FSRRA")]

def split(text):
    top = text[:text.index("#ifndef SH4_")]                                   # the leading documentation comment
    body = text[text.index('extern "C" {\n#endif\n') + len('extern "C" {\n#endif\n'):text.rindex("#ifdef __cplusplus\n}")]
    return top, body

def strip_core(body, p):
    n0 = len(body)
    body, a = re.subn(r"(?:/\*[^/]*?\*/\n)?static inline int64_t sh4_%s__asr\([^\n]*\n" % p, "", body)
    body, b = re.subn(r"(?:/\*[^/]*?\*/\n)?static inline int64_t sh4_%s__arr\(.*?\n}\n" % p, "", body, flags=re.S)
    body, c = re.subn(r"(?:/\*[^/]*?\*/\n)?static inline int64_t sh4_%s__sm\([^\n]*\n" % p, "", body)
    assert a == 1 and b == 1 and c <= 1, (p, a, b, c)
    body = re.sub(r"sh4_%s__(asr|arr|sm)\b" % p, r"sh4_fpu__\1", body)
    return body.strip("\n") + "\n"

parts = []
for p, path, title in SRC:
    top, body = split(open(os.path.join(model, path)).read())
    doc = top.strip()
    doc = re.sub(r"^/\* sh4_\w+\.h -- ", "/* ---------------------------------------------------------------------------------------------------------\n * " + title + " -- ", doc)
    doc = doc.replace(" * Single header, C99, no dependencies.\n", "")
    parts.append(doc + "\n" + strip_core(body, p))

out = '''/* sh4_fpu_approx.h -- bit-exact models of the SH7091 / SH7750 (SH-4) vector and approximation FPU instructions:
 *     FIPR (inner product), FTRV (matrix x vector), FSCA (sine / cosine), FSRRA (reciprocal square root).
 * Single header, C99, no dependencies.  GENERATED from sh4_fipr.h, sh4_fsca.h and sh4_fsrra.h by tools/make_fpu_approx.py:
 * the instruction code is identical to those headers, with the multiplier shared.  Do not include it together with them
 * (it defines their include guards, so including them AFTER this file is harmless).
 *
 * All four instructions run on ONE hardware block -- four truncated Booth radix-4 multipliers, an aligner per product, a
 * four-input adder and one normalise / round stage (Hitachi US 6,038,582).  FIPR uses it once; FTRV is four FIPRs; FSCA and
 * FSRRA are 4- and 3-step microprograms that evaluate a cubic around a table node through the SAME multipliers, feeding
 * product registers back as operands.  That is why one array model, below, serves all of them.
 *
 *     uint32_t sh4_fipr (fvm[4], fvn[4], rz)                 int sh4_fipr_ex (fvm, fvn, &fpscr, &r)
 *     void     sh4_ftrv (xmtrx[16], fv[4], rz)               int sh4_ftrv_ex (xmtrx, fv, &fpscr)
 *     void     sh4_fsca (fpul, rz, &sin, &cos)               int sh4_fsca_ex (fpul, &fpscr, &sin, &cos)
 *     uint32_t sh4_fsrra(x, rz)                              int sh4_fsrra_ex(x, &fpscr, &r)
 * Values are IEEE-754 bit patterns; rz = (FPSCR.RM == 1).  The plain functions return the value (FPSCR.DN = 1 behaviour,
 * no trap taken).  The _ex functions implement the measured FPSCR semantics -- cause field rewritten, flag field OR-ed,
 * DN, PR -- and return 1 when the instruction raises an FPU exception trap, leaving the destination untouched.
 *
 * Everything here was verified against a real Dreamcast with 0 mismatches; the per-instruction sections say on what.
 * In short, which enables make each instruction trap UNCONDITIONALLY (PR = 0 / PR = 1, where all four are no-ops):
 *     FSRRA  I / I       FSCA  I / I       FIPR  O,U,I / I       FTRV  V,O,U,I / V,O,U,I
 */
#ifndef SH4_FPU_APPROX_H
#define SH4_FPU_APPROX_H

#if defined(SH4_FIPR_H) || defined(SH4_FSCA_H) || defined(SH4_FSRRA_H)
#error "include sh4_fpu_approx.h INSTEAD of sh4_fipr.h / sh4_fsca.h / sh4_fsrra.h (or before them)"
#endif
#define SH4_FIPR_H
#define SH4_FSCA_H
#define SH4_FSRRA_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------
 * The shared core. */

/* arithmetic shift right that does not rely on implementation-defined behaviour */
static inline int64_t sh4_fpu__asr(int64_t x, int n) { return x < 0 ? ~(~x >> n) : x >> n; }

/* The truncated Booth radix-4 multiplier array, before the rounding constant 2^19 that every product register adds.
 * rec = recoded operand (28 bits signed), mul = multiplicand (25 bits signed).  Partial-product rows are cut below column
 * 16, a negative row is a one's complement whose hot one is only present from column 18 up, rows 0-2 contribute a majority
 * carry from column 15 and rows 6-8 lose the parity of their column-16 bits.  Rows 9 and up are therefore exact and are
 * taken as one ordinary product.  Not monotone in rec. */
static inline int64_t sh4_fpu__arr(int64_t rec, int64_t mul) {
    uint32_t lo = (uint32_t)rec & 0x3ffffu;                          /* bits 17..0 feed digits 0..8 */
    int64_t  slo = (int64_t)lo - ((lo & 0x20000u) ? 0x40000 : 0);    /* value of digits 0..8 = signed low 18 bits */
    int64_t  T = (rec - slo) * mul;                                  /* digits 9..13: exact */
    uint32_t bits = lo << 1;                                         /* bit -1 = 0 */
    int m15 = 0, p16 = 0, k;
    for (k = 0; k < 9; k++, bits >>= 2) {
        int d = (int)(bits & 1u) + (int)((bits >> 1) & 1u) - 2 * (int)((bits >> 2) & 1u);
        int64_t mag = (int64_t)(d < 0 ? -d : d) * mul;
        int64_t row = (d < 0 ? ~mag : mag) * ((int64_t)1 << (2 * k));
        T += sh4_fpu__asr(row, 16) * 65536;
        if (k < 3)  m15 += (int)(sh4_fpu__asr(row, 15) & 1);
        if (k >= 6) p16 ^= (int)(sh4_fpu__asr(row, 16) & 1);
    }
    if (m15 >= 2) T += 65536;
    if (p16)      T -= 65536;
    return T;
}

/* the aligner in front of the four-input adder: truncation of a signed product, "every negative input gets a hot one" */
static inline int64_t sh4_fpu__sm(int64_t P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }

''' + "\n".join(parts) + '''
#ifdef __cplusplus
}
#endif
#endif /* SH4_FPU_APPROX_H */
'''
assert not re.search(r"sh4_(fipr|fsca|fsrra)__(asr|arr|sm)\b", out)
open(os.path.join(model, "sh4_fpu_approx.h"), "w").write(out)
print("wrote sh4_fpu_approx.h: %d lines" % out.count("\n"))
