// sh4_fpdn.h -- bit-exact SH7091 scalar FPU arithmetic with FPSCR.DN = 0 (gradual underflow, the FPU-error E trap)
// and DN = 1, as measured on the console (tools/hw/fpdn, SOC.md 9.17; WI-FPDN).  Header-only C++17, integer soft-float:
// no host FPU, one rounder, so it is independent of both tb/sh4_iss.h (host FPU) and the RTL (rtl/v1/v1_fp.sv).
//
// Values are IEEE bit patterns (a single in the low 32 bits).  Operand order is the instruction's: a = FRn / DRn,
// b = FRm / DRm (FDIV: a / b; FCMP/GT: a > b); FMAC: a = FR0, b = FRm, c = FRn (a * b + c).
// Every operation returns {value, cause}: cause bits C_I 1, C_U 2, C_O 4, C_Z 8, C_V 16, C_E 32 = FPSCR cause 12..17.
// C_E means the instruction takes the FPU-error trap (never maskable): the value is then meaningless.
// exec() is the instruction level (FPSCR in, trap / FPSCR / destination out) that tools/hw/fpdn records are checked with.
//
// What the console shows (fpdn_run1: 36,750 class-table records under 7 FPSCR settings, 172,032 value records):
//  * DN = 0, operand side: a denormal operand is a nonzero finite number.  E is raised by FADD FSUB FMUL FDIV FMAC FSQRT
//    FCNVDS FCNVSD (and FSRRA, model/fsrra) when a denormal operand reaches the arithmetic, after the NaN and invalid
//    checks: a signalling NaN (V) and, in single precision, a quiet NaN win over E; FDIV x / +-0 is a division by zero
//    (Z) even when x is denormal; FMAC's 0 * inf and (inf product) - inf are invalid (V) before E; FSQRT of a negative
//    denormal is invalid (V, like any negative).  FTRC and FCMP never raise E (the manual lists E for FTRC: it does
//    not): FTRC truncates a denormal to 0 with no cause, FCMP compares denormals exactly.  FIPR / FTRV compute with
//    denormals (model/fipr), FSCA has no FP operand.
//  * The E trap: the destination is not written, FPSCR.cause = E alone, the flag field is unchanged (E has no flag),
//    whatever the enables (with EN.I set the trap's cause is still E).
//  * DN = 0, result side: gradual underflow, rounded once on the denormal grid (RN / RZ).  See round_pack for U / I.
//  * DN = 1: a denormal operand is a zero of its sign (not for FABS / FNEG / moves); a result below the minimum normal
//    after rounding on the denormal grid is flushed to the zero of its sign with U | I (v1_fp_pkg's rule).
#pragma once
#include <cstdint>
#include <cstring>

namespace sh4fpdn {
using u32 = uint32_t;
using u64 = uint64_t;
using u128 = unsigned __int128;

enum : u32 { C_I = 1, C_U = 2, C_O = 4, C_Z = 8, C_V = 16, C_E = 32 };
struct R { u64 v; u32 c; };

static const u32 QNAN_S = 0x7FBFFFFFu;
static const u64 QNAN_D = 0x7FF7FFFFFFFFFFFFull;

// ---- tininess (the U cause) -- selectable so the console data can decide; the measured rule is the default --------
enum Tiny { TINY_GRID, TINY_BEFORE, TINY_IEEE_AFTER };
inline Tiny &tiny_rule() { static Tiny t = TINY_GRID; return t; }

// ---- formats and classes -------------------------------------------------------------------------------------------
struct Fmt { int p, bias, emin, emax, ebits; u64 fmask; };
inline const Fmt &fmt(bool dbl) {
    static const Fmt S{24, 127, -126, 127, 8, 0x7FFFFFull}, D{53, 1023, -1022, 1023, 11, 0xFFFFFFFFFFFFFull};
    return dbl ? D : S;
}
struct Cls { bool s, zero, den, inf, nan, snan; };
// the class of an operand; dn = FPSCR.DN (a denormal is a zero with DN = 1)
inline Cls cls(u64 x, bool dbl, bool dn) {
    const Fmt &F = fmt(dbl);
    const int nb = dbl ? 64 : 32;
    const u64 ex = (x >> (F.p - 1)) & ((1ull << F.ebits) - 1), fr = x & F.fmask;
    Cls c{};
    c.s = (x >> (nb - 1)) & 1;
    const bool emax = ex == (1ull << F.ebits) - 1;
    c.nan = emax && fr;
    c.snan = c.nan && ((fr >> (F.p - 2)) & 1);       // SH-4: the fraction MSB = 1 is signalling
    c.inf = emax && !fr;
    c.den = !ex && fr && !dn;
    c.zero = !ex && (!fr || dn);
    return c;
}
// The double's two words (DN = 0; fpdn_run2): the special-case logic classifies a double from its HIGH word -- a NaN
// with a fraction bit in the high word (hnan), a denormal with one there (hden) -- and the low word only refines a
// high-word zero into a denormal (lden: fraction in the low word only) and a high-word infinity into a NaN (a quiet NaN
// with the fraction in the low word only), one step later in the priority order.
inline bool hnan(u64 x) { return ((x >> 52) & 0x7FF) == 0x7FF && ((x >> 32) & 0xFFFFF); }
inline bool hden(u64 x) { return ((x >> 52) & 0x7FF) == 0 && ((x >> 32) & 0xFFFFF); }
inline bool lden(u64 x) { return ((x >> 52) & 0x7FF) == 0 && !((x >> 32) & 0xFFFFF) && (u32)x; }
inline u64 k_qnan(bool dbl) { return dbl ? QNAN_D : QNAN_S; }
inline u64 k_inf(bool s, bool dbl) { return dbl ? ((u64)s << 63) | 0x7FF0000000000000ull : ((u64)s << 31) | 0x7F800000u; }
inline u64 k_zero(bool s, bool dbl) { return dbl ? (u64)s << 63 : (u64)s << 31; }
inline u64 k_max(bool s, bool dbl) { return dbl ? ((u64)s << 63) | 0x7FEFFFFFFFFFFFFFull : ((u64)s << 31) | 0x7F7FFFFFu; }

// a finite nonzero operand as m * 2^e (a denormal: no hidden bit, the minimum exponent)
struct Num { bool s; int e; u128 m; };
inline Num num(u64 x, bool dbl) {
    const Fmt &F = fmt(dbl);
    const int nb = dbl ? 64 : 32;
    const u64 ex = (x >> (F.p - 1)) & ((1ull << F.ebits) - 1), fr = x & F.fmask;
    Num n;
    n.s = (x >> (nb - 1)) & 1;
    if (ex) { n.m = fr | (1ull << (F.p - 1)); n.e = (int)ex - F.bias - (F.p - 1); }
    else    { n.m = fr;                       n.e = 1 - F.bias - (F.p - 1); }
    return n;
}
inline int blen(u128 m) { int l = 0; while (m) { m >>= 1; l++; } return l; }

// ---- the rounder ---------------------------------------------------------------------------------------------------
// Rounds (-1)^s * (m + t) * 2^e, m > 0, 0 < t < 1/2 when st (every caller keeps at least two bits below the result's
// last place, so a sticky can never be a half) -- on the denormal grid: the quantum is 2^(E - p + 1) for a value of
// exponent E >= emin, and 2^(emin - p + 1) below.  RN: nearest, ties to even; RZ: truncate.  I = inexact.
// Overflow (the rounded exponent > emax): O | I, +-inf (RN) / +-max finite (RZ).
// DN = 0: a result below the minimum normal is a denormal (or zero); U = tiny and inexact, tiny per tiny_rule():
//   GRID      the rounded (denormal-grid) result is below the minimum normal
//   BEFORE    the exact value is below the minimum normal
//   IEEE_AFTER the value rounded to p bits with an unbounded exponent is below the minimum normal
// DN = 1: a nonzero exact value whose grid-rounded result is below the minimum normal is the zero of its sign, U | I.
inline R round_pack(bool s, int e, u128 m, bool st, bool dbl, bool rz, bool dn) {
    const Fmt &F = fmt(dbl);
    const int L = blen(m), E = e + L - 1;
    const int qn = E - (F.p - 1), qd = F.emin - (F.p - 1), q0 = qn > qd ? qn : qd;
    auto rnd = [&](int q, u128 &kept, bool &inx) {       // round m * 2^e to a multiple of 2^q
        const int sh = q - e;
        if (sh <= 0) { kept = m << (-sh); inx = st; return; }
        if (sh > 128) { kept = 0; inx = true; return; }   // m < 2^128 < half a quantum
        u128 rem, half;
        if (sh == 128) { kept = 0; rem = m; half = ((u128)1) << 127; }
        else { kept = m >> sh; rem = m & ((((u128)1) << sh) - 1); half = ((u128)1) << (sh - 1); }
        inx = rem || st;
        if (!rz) kept += rem > half || (rem == half && (st || (kept & 1)));
    };
    u128 kept; bool inx;
    int q = q0;
    rnd(q, kept, inx);
    if (kept >> F.p) { kept >>= 1; q++; }                 // carry out of the kept field
    const bool below = kept < ((u128)1 << (F.p - 1));     // grid result is a denormal or zero
    R r{0, 0};
    if (!below && q + F.p - 1 > F.emax) {                 // overflow
        r.c = C_O | C_I;
        r.v = rz ? k_max(s, dbl) : k_inf(s, dbl);
        return r;
    }
    if (dn) {
        if (below) { r.v = k_zero(s, dbl); r.c = C_U | C_I; return r; }
    }
    u64 bits;
    if (below) bits = (u64)kept;                          // exponent field 0
    else bits = ((u64)(q + F.p - 1 + F.bias) << (F.p - 1)) | ((u64)kept & F.fmask);
    r.v = bits | ((u64)s << (dbl ? 63 : 31));
    if (inx) {
        r.c |= C_I;
        bool tiny = false;
        switch (tiny_rule()) {
        case TINY_GRID: tiny = below; break;
        case TINY_BEFORE: tiny = E < F.emin; break;
        case TINY_IEEE_AFTER: {
            if (E >= F.emin) { tiny = false; break; }
            if (E + 1 < F.emin) { tiny = true; break; }
            u128 k2; bool i2; rnd(qn, k2, i2);            // p bits at the value's own exponent (E = emin - 1)
            tiny = !(k2 >> F.p);                          // no carry up to 2^(E + 1) = the minimum normal
            break;
        }
        }
        if (tiny) r.c |= C_U;
    }
    return r;
}

// ---- exact adder of two finite nonzero numbers ------------------------------------------------------------------------
// both aligned into a 128-bit frame with the larger one's leading bit at 125; a smaller operand shifted out entirely is a
// sticky (its magnitude is below the frame's last bit, far below any rounding position)
inline R add_num(Num x, Num y, bool dbl, bool rz, bool dn) {
    int Ex = x.e + blen(x.m) - 1, Ey = y.e + blen(y.m) - 1;
    if (Ey > Ex) { Num t = x; x = y; y = t; int u = Ex; Ex = Ey; Ey = u; }   // x: the larger leading exponent
    const int fe = Ex - 125;                              // frame: value = M * 2^fe
    u128 Mx = x.m << (x.e - fe), My;
    bool st = false;                                      // y's bits below the frame (only if Ey < Ex)
    const int sh = y.e - fe;
    if (sh >= 0) My = y.m << sh;
    else if (-sh >= 127) { My = 0; st = true; }
    else { My = y.m >> (-sh); st = (y.m & ((((u128)1) << (-sh)) - 1)) != 0; }
    if (x.s == y.s) return round_pack(x.s, fe, Mx + My, st, dbl, rz, dn);
    if (Mx == My && !st) return R{k_zero(false, dbl), 0};     // x + (-x) = +0 (RN, RZ)
    if (My > Mx) return round_pack(y.s, fe, My - Mx, false, dbl, rz, dn);   // same leading exponent: no sticky
    return round_pack(x.s, fe, Mx - My - (st ? 1 : 0), st, dbl, rz, dn);   // exact = that + (1 - t)
}
inline R mul_num(const Num &x, const Num &y, bool dbl, bool rz, bool dn) {
    return round_pack(x.s ^ y.s, x.e + y.e, x.m * y.m, false, dbl, rz, dn);
}
inline R div_num(const Num &x, const Num &y, bool dbl, bool rz, bool dn) {
    const int S = 120 - blen(x.m);
    const u128 n = x.m << S, q = n / y.m, rm = n % y.m;
    return round_pack(x.s ^ y.s, x.e - S - y.e, q, rm != 0, dbl, rz, dn);
}
inline u128 isqrt(u128 v, bool &exact) {
    u128 r = 0, bit = ((u128)1) << 126;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    exact = v == 0;
    return r;
}
inline R sqrt_num(Num x, bool dbl, bool rz, bool dn) {
    if (x.e & 1) { x.m <<= 1; x.e -= 1; }
    int k = (124 - blen(x.m)) / 2;
    bool ex;
    const u128 r = isqrt(x.m << (2 * k), ex);
    return round_pack(false, (x.e - 2 * k) / 2, r, !ex, dbl, rz, dn);
}

// ---- the operations ------------------------------------------------------------------------------------------------
// FADD / FSUB (b already negated for FSUB)
inline R fadd(u64 a, u64 b, bool dbl, bool rz, bool dn) {
    const Cls A = cls(a, dbl, dn), B = cls(b, dbl, dn);
    if (A.snan || B.snan) return {k_qnan(dbl), C_V};
    // a quiet NaN beats E in single precision; in double only when its fraction shows in the high word
    if (!dbl && (A.nan || B.nan)) return {k_qnan(dbl), 0};
    if (dbl && (hnan(a) || hnan(b))) return {k_qnan(dbl), 0};
    if (A.den || B.den) return {0, C_E};
    if (A.nan || B.nan) return {k_qnan(dbl), 0};
    if (A.inf && B.inf) return A.s != B.s ? R{k_qnan(dbl), C_V} : R{k_inf(A.s, dbl), 0};
    if (A.inf) return {k_inf(A.s, dbl), 0};
    if (B.inf) return {k_inf(B.s, dbl), 0};
    if (A.zero && B.zero) return {k_zero(A.s && B.s, dbl), 0};
    if (A.zero) return {dbl ? b : (b & 0xFFFFFFFFull), 0};
    if (B.zero) return {dbl ? a : (a & 0xFFFFFFFFull), 0};
    return add_num(num(a, dbl), num(b, dbl), dbl, rz, dn);
}
inline u64 neg(u64 b, bool dbl) { return b ^ (dbl ? 0x8000000000000000ull : 0x80000000ull); }
inline R fsub(u64 a, u64 b, bool dbl, bool rz, bool dn) { return fadd(a, neg(b, dbl), dbl, rz, dn); }

inline R fmul(u64 a, u64 b, bool dbl, bool rz, bool dn) {
    const Cls A = cls(a, dbl, dn), B = cls(b, dbl, dn);
    const bool s = A.s ^ B.s;
    if (A.snan || B.snan) return {k_qnan(dbl), C_V};
    if (!dbl || dn) {                                      // (DN = 1: no denormal operand exists)
        if (A.nan || B.nan) return {k_qnan(dbl), 0};
        if (A.den || B.den) return {0, C_E};
    } else {
        // high-word NaN, high-word denormal (E), then the low-word NaN and the infinities before a low-word-only
        // denormal's E: such a denormal times inf is inf (it is nonzero), never 0 * inf
        if (hnan(a) || hnan(b)) return {k_qnan(dbl), 0};
        if (hden(a) || hden(b)) return {0, C_E};
        if (A.nan || B.nan) return {k_qnan(dbl), 0};
        if ((A.inf && B.zero) || (A.zero && B.inf)) return {k_qnan(dbl), C_V};
        if (A.inf || B.inf) return {k_inf(s, dbl), 0};
        if (A.den || B.den) return {0, C_E};
    }
    if ((A.inf && B.zero) || (A.zero && B.inf)) return {k_qnan(dbl), C_V};
    if (A.inf || B.inf) return {k_inf(s, dbl), 0};
    if (A.zero || B.zero) return {k_zero(s, dbl), 0};
    return mul_num(num(a, dbl), num(b, dbl), dbl, rz, dn);
}

// FDIV a / b
inline R fdiv(u64 a, u64 b, bool dbl, bool rz, bool dn) {
    const Cls A = cls(a, dbl, dn), B = cls(b, dbl, dn);
    const bool s = A.s ^ B.s;
    if (A.snan || B.snan) return {k_qnan(dbl), C_V};
    if (dbl && !dn) {
        // FDIV DR with DN = 0 (fpdn_run2): high-word NaN; a zero divisor (Z; a low-word-only denormal dividend gives
        // the signed zero, no cause: the high words read 0 / 0, the low word turns "invalid" into "not invalid");
        // high-word denormals (E); the dividend's low-word NaN, its low-word-only denormal (0 over a high-word zero,
        // else E), the divisor's low-word NaN; then the ordinary cases, a low-word-only denormal divisor last (E)
        if (hnan(a) || hnan(b)) return {k_qnan(dbl), 0};
        if (B.zero) {
            if (A.nan) return {k_qnan(dbl), 0};
            if (A.zero) return {k_qnan(dbl), C_V};
            if (lden(a)) return {k_zero(s, dbl), 0};
            if (A.inf) return {k_inf(s, dbl), 0};
            return {k_inf(s, dbl), C_Z};
        }
        if (hden(a) || hden(b)) return {0, C_E};
        if (A.nan) return {k_qnan(dbl), 0};
        if (lden(a)) return lden(b) ? R{k_zero(s, dbl), 0} : R{0, C_E};
        if (B.nan) return {k_qnan(dbl), 0};
        if (A.inf) return B.inf ? R{k_qnan(dbl), C_V} : R{k_inf(s, dbl), 0};
        if (A.zero || B.inf) return {k_zero(s, dbl), 0};
        if (lden(b)) return {0, C_E};
        return div_num(num(a, dbl), num(b, dbl), dbl, rz, dn);
    }
    if (!dbl && (A.nan || B.nan)) return {k_qnan(dbl), 0};
    if (B.zero) {                                          // x / 0: before E (a denormal dividend is a nonzero x)
        if (A.nan) return {k_qnan(dbl), 0};
        if (A.zero) return {k_qnan(dbl), C_V};
        if (A.inf) return {k_inf(s, dbl), 0};
        return {k_inf(s, dbl), C_Z};
    }
    if (A.den || B.den) return {0, C_E};
    if (A.nan || B.nan) return {k_qnan(dbl), 0};
    if (A.inf && B.inf) return {k_qnan(dbl), C_V};
    if (A.inf) return {k_inf(s, dbl), 0};
    if (B.inf || A.zero) return {k_zero(s, dbl), 0};
    return div_num(num(a, dbl), num(b, dbl), dbl, rz, dn);
}

inline R fsqrt(u64 a, bool dbl, bool rz, bool dn) {
    const Cls A = cls(a, dbl, dn);
    if (A.nan) return {k_qnan(dbl), A.snan ? C_V : 0u};
    if (A.zero) return {k_zero(A.s, dbl), 0};              // +-0 (DN = 1: a denormal too)
    if (A.s) return {k_qnan(dbl), C_V};                    // negative: normal, denormal, -inf
    if (A.den) return {0, C_E};
    if (A.inf) return {a, 0};
    return sqrt_num(num(a, dbl), dbl, rz, dn);
}

// FMAC FR0, FRm, FRn: a * b + c, single, one rounding
inline R fmac(u32 a, u32 b, u32 c, bool rz, bool dn) {
    const Cls A = cls(a, false, dn), B = cls(b, false, dn), Cc = cls(c, false, dn);
    const bool sp = A.s ^ B.s;
    if (A.snan || B.snan || Cc.snan) return {QNAN_S, C_V};
    if (A.nan || B.nan) return {QNAN_S, 0};
    if ((A.inf && B.zero) || (A.zero && B.inf)) return {QNAN_S, C_V};   // 0 * inf, also with a qNaN addend
    if (Cc.nan) return {QNAN_S, 0};
    const bool pinf = A.inf || B.inf;                      // a denormal factor times inf is inf
    if (pinf && Cc.inf && Cc.s != sp) return {QNAN_S, C_V};
    if (A.den || B.den || Cc.den) return {0, C_E};
    if (pinf) return {k_inf(sp, false), 0};
    if (Cc.inf) return {c, 0};
    if (A.zero || B.zero) return Cc.zero ? R{k_zero(sp && Cc.s, false), 0} : R{c, 0};
    Num p = num(a, false), y = num(b, false);
    p.m *= y.m; p.e += y.e; p.s = sp;
    if (Cc.zero) return round_pack(sp, p.e, p.m, false, false, rz, dn);
    return add_num(p, num(c, false), false, rz, dn);
}

// FTRC: -> int32 (truncation), never E; a denormal is 0 with no cause (DN = 0 and DN = 1 alike)
inline R ftrc(u64 a, bool dbl) {
    const u32 hi = dbl ? (u32)(a >> 32) : (u32)a;
    bool nan_p, big_p, big_n;
    if (dbl) { nan_p = hi > 0x7FF00000u || (hi == 0x7FF00000u && (u32)a); big_p = hi > 0x41DFFFFFu; big_n = hi > 0xC1E00000u; }
    else     { nan_p = hi > 0x7F800000u; big_p = hi > 0x4EFFFFFFu; big_n = hi > 0xCF000000u; }
    if (!(hi >> 31) && nan_p) return {0x80000000u, C_V};
    if (!(hi >> 31) && big_p) return {0x7FFFFFFFu, C_V};
    if ((hi >> 31) && big_n) return {0x80000000u, C_V};
    const Fmt &F = fmt(dbl);
    const u64 ex = (a >> (F.p - 1)) & ((1ull << F.ebits) - 1);
    if (!ex) return {0, 0};
    const Num n = num(a, dbl);
    if (n.e >= 0) return {n.s ? (u32)(-(int64_t)(u64)(n.m << n.e)) : (u32)(u64)(n.m << n.e), 0};
    if (-n.e >= 64) return {0, 0};
    u64 mag = (u64)(n.m >> (-n.e));
    if (n.s && mag > 0x80000000ull) mag = 0x80000000ull;  // the high-word window below -2^31: saturates, no V (v1_fp)
    return {n.s ? (u32)(0u - (u32)mag) : (u32)mag, 0};
}

// FCMP: T; the V cause (EQ: a signalling NaN; GT: any NaN); denormals compare exactly with DN = 0, as zeros with DN = 1
struct Cmp { bool t; u32 c; };
inline Cmp fcmp(u64 a, u64 b, bool dbl, bool dn, bool gt) {
    const Cls A = cls(a, dbl, dn), B = cls(b, dbl, dn);
    if (A.nan || B.nan) return {false, (gt || A.snan || B.snan) ? C_V : 0u};
    auto key = [&](u64 x, const Cls &C) -> __int128 {
        if (C.zero) return 0;
        const u64 mag = dbl ? (x & 0x7FFFFFFFFFFFFFFFull) : (x & 0x7FFFFFFFull);
        return C.s ? -(__int128)mag : (__int128)mag;
    };
    const __int128 ka = key(a, A), kb = key(b, B);
    return {gt ? ka > kb : ka == kb, 0};
}

// FCNVSD FPUL, DRn (exact) / FCNVDS DRm, FPUL (rounds)
inline R fcnvsd(u32 a, bool dn) {
    const Cls A = cls(a, false, dn);
    if (A.nan) return {QNAN_D, A.snan ? C_V : 0u};
    if (A.den) return {0, C_E};
    if (A.inf) return {k_inf(A.s, true), 0};
    if (A.zero) return {k_zero(A.s, true), 0};
    const Num n = num(a, false);
    return round_pack(n.s, n.e, n.m, false, true, false, dn);
}
inline R fcnvds(u64 a, bool rz, bool dn) {
    const Cls A = cls(a, true, dn);
    if (A.nan) return {QNAN_S, A.snan ? C_V : 0u};
    if (A.den) return {0, C_E};
    if (A.inf) return {k_inf(A.s, false), 0};
    if (A.zero) return {k_zero(A.s, false), 0};
    const Num n = num(a, true);
    return round_pack(n.s, n.e, n.m, false, false, rz, dn);
}

// ---- instruction level (the forms of tools/hw/fpdn) ------------------------------------------------------------------
// FPSCR: RM 1:0, flags 6:2 (I U O Z V), enables 11:7, cause 17:12 (I U O Z V E), DN 18, PR 19
enum Op { FADD_S, FSUB_S, FMUL_S, FDIV_S, FCMPEQ_S, FCMPGT_S, FMAC_S, FSQRT_S, FTRC_S, FABS_S, FNEG_S, FSRRA_S,
          FADD_D, FSUB_D, FMUL_D, FDIV_D, FCMPEQ_D, FCMPGT_D, FSQRT_D, FTRC_D, FABS_D, FNEG_D, FCNVDS, FCNVSD, NOPS };
// the enables that trap unconditionally ("possibility", exceptions-intc 3.8): O / U / I
inline u32 oui_mask(int op) {
    switch (op) {
    case FADD_S: case FSUB_S: case FMUL_S: case FDIV_S: case FMAC_S: case FADD_D: case FSUB_D: case FMUL_D: case FDIV_D:
    case FCNVDS: return C_I | C_U | C_O;
    case FSQRT_S: case FSQRT_D: return C_I;
    default: return 0;
    }
}
struct Mach { u32 fr[4]; u32 fpul, fpscr, t; bool trap; };
// one scalar operation's result and where it goes: wr 1 FR0, 2 DR0, 3 FPUL, 4 FR2 (FMAC), 5 T (FCMP)
struct OpRes { R r; int wr; bool t; };
inline OpRes eval(int op, const u32 in[5], u32 fpscr) {
    const bool rz = fpscr & 1, dn = (fpscr >> 18) & 1;
    const u64 da = ((u64)in[0] << 32) | in[1], db = ((u64)in[2] << 32) | in[3];
    OpRes o{{0, 0}, 0, false};
    switch (op) {
    case FADD_S: o.r = fadd(in[0], in[1], false, rz, dn); o.wr = 1; break;
    case FSUB_S: o.r = fsub(in[0], in[1], false, rz, dn); o.wr = 1; break;
    case FMUL_S: o.r = fmul(in[0], in[1], false, rz, dn); o.wr = 1; break;
    case FDIV_S: o.r = fdiv(in[0], in[1], false, rz, dn); o.wr = 1; break;
    case FCMPEQ_S: case FCMPGT_S: { Cmp c = fcmp(in[0], in[1], false, dn, op == FCMPGT_S); o.r.c = c.c; o.t = c.t; o.wr = 5; break; }
    case FMAC_S: o.r = fmac(in[0], in[1], in[2], rz, dn); o.wr = 4; break;
    case FSQRT_S: o.r = fsqrt(in[0], false, rz, dn); o.wr = 1; break;
    case FTRC_S: o.r = ftrc(in[0], false); o.wr = 3; break;
    case FADD_D: o.r = fadd(da, db, true, rz, dn); o.wr = 2; break;
    case FSUB_D: o.r = fsub(da, db, true, rz, dn); o.wr = 2; break;
    case FMUL_D: o.r = fmul(da, db, true, rz, dn); o.wr = 2; break;
    case FDIV_D: o.r = fdiv(da, db, true, rz, dn); o.wr = 2; break;
    case FCMPEQ_D: case FCMPGT_D: { Cmp c = fcmp(da, db, true, dn, op == FCMPGT_D); o.r.c = c.c; o.t = c.t; o.wr = 5; break; }
    case FSQRT_D: o.r = fsqrt(da, true, rz, dn); o.wr = 2; break;
    case FTRC_D: o.r = ftrc(da, true); o.wr = 3; break;
    case FCNVDS: o.r = fcnvds(da, rz, dn); o.wr = 3; break;
    case FCNVSD: o.r = fcnvsd(in[4], dn); o.wr = 2; break;
    default: break;
    }
    return o;
}
// the instruction's effect [HW fpdn]: a trap -- E (never maskable), V / Z with their enables, or an O / U / I enable
// the operation can raise ("possibility") -- leaves the destination alone; cause <- the operation's cause (E alone for
// E), flags |= its V Z O U I.  (With an O / U / I enable the console reports the computed O / U / I too: FSQRT(1.5)
// with EN.I traps with cause and flag I, 0x00001084.)  Otherwise the destination is written, cause rewritten, flags OR.
inline Mach compose(int op, const u32 in[5], u32 fpscr, u32 t_in, const OpRes &o) {
    Mach m{};
    for (int i = 0; i < 4; i++) m.fr[i] = in[i];
    m.fpul = in[4]; m.fpscr = fpscr; m.t = t_in; m.trap = false;
    const R &r = o.r;
    const u32 en = (fpscr >> 7) & 0x1F;                    // I U O Z V
    m.fpscr = (fpscr & ~0x3F000u) | (r.c << 12) | ((r.c & 0x1F) << 2);
    if ((r.c & C_E) || (en & r.c & (C_V | C_Z)) || (en & oui_mask(op))) { m.trap = true; return m; }
    switch (o.wr) {
    case 1: m.fr[0] = (u32)r.v; break;
    case 2: m.fr[0] = (u32)(r.v >> 32); m.fr[1] = (u32)r.v; break;
    case 3: m.fpul = (u32)r.v; break;
    case 4: m.fr[2] = (u32)r.v; break;
    case 5: m.t = o.t; break;
    }
    return m;
}
// the full value of one operation on the machine state; fsrra: a callback for FSRRA (model/fsrra), or nullptr
typedef int (*fsrra_fn)(u32 x, u32 *fpscr, u32 *r);
inline Mach exec(int op, const u32 in[5], u32 fpscr, u32 t_in, fsrra_fn fsrra) {
    Mach m{};
    for (int i = 0; i < 4; i++) m.fr[i] = in[i];
    m.fpul = in[4]; m.fpscr = fpscr; m.t = t_in; m.trap = false;
    switch (op) {
    case FABS_S: case FABS_D: m.fr[0] = in[0] & 0x7FFFFFFFu; return m;   // copies: DN never matters
    case FNEG_S: case FNEG_D: m.fr[0] = in[0] ^ 0x80000000u; return m;
    case FSRRA_S: {
        u32 f = fpscr, res = in[0];
        const int trap = fsrra ? fsrra(in[0], &f, &res) : 0;
        m.fpscr = f; m.trap = trap != 0; m.fr[0] = res;
        return m;
    }
    default: return compose(op, in, fpscr, t_in, eval(op, in, fpscr));
    }
}
} // namespace sh4fpdn
