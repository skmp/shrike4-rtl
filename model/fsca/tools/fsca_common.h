// Shared: FIPR multiplier (exact, from ../../fipr/fipr_model.cpp) and FSCA dump decoding.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
typedef int64_t i64; typedef uint64_t u64;
static inline int booth_digit(uint32_t x, int k) { int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1; return -2*b1 + b0 + b_1; }
// rec = Booth-recoded operand (FVn), mul = multiplicand (FVm); both < 2^24; returns 28-bit product ~ rec*mul >> 20
static i64 g_K = 1ll << 19;   // multiplier rounding constant (2^19 in FIPR); 0 = raw truncated product
static inline i64 mult28(uint32_t rec, uint32_t mul) {
    u64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { int d = booth_digit(rec, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag;
        R[k] = oc << (2*k); T += (i64)((R[k] >> 16) << 16); if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); } }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2);
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16;
    return (T + g_K) >> 20;
}
struct Op { uint32_t m; int e; };            // value = m * 2^(e-23), m < 2^24
static inline int msb(u64 x) { return 63 - __builtin_clzll(x); }
static inline Op op_norm(u64 v) { int b = msb(v); return b >= 23 ? Op{ (uint32_t)(v >> (b - 23)), b } : Op{ (uint32_t)(v << (23 - b)), b }; }   // truncating normalisation
static inline Op op_fixed(u64 v, int sh) { return Op{ (uint32_t)(v << sh), 23 - sh }; }
// product of two operands on the 2^-26 grid (units), magnitude, truncated: T = p28 * 2^(ea+eb)
static inline i64 prod_units(Op rec, Op mul) { i64 p = mult28(rec.m, mul.m); int sh = rec.e + mul.e; return sh >= 0 ? (p << sh) : (p >> (-sh)); }
struct Fsca { std::vector<uint32_t> srn, crn, srz, crz; };
static inline bool load_fsca(Fsca& F, const char* path = "../../fipr/kos_fpu_dump/fsca_rnrz.bin") {
    FILE* f = fopen(path, "rb"); if (!f) return false; std::vector<uint32_t> b(65536 * 4); if (fread(b.data(), 4, b.size(), f) != b.size()) return false; fclose(f);
    F.srn.resize(65536); F.crn.resize(65536); F.srz.resize(65536); F.crz.resize(65536);
    for (int i = 0; i < 65536; i++) { F.srn[i] = b[4*i]; F.crn[i] = b[4*i+1]; F.srz[i] = b[4*i+2]; F.crz[i] = b[4*i+3]; } return true; }
// window of the internal magnitude (units of 2^-26) implied by an RZ float and an RN float: [lo, hi]
struct Win { i64 lo, hi; bool exact; int sign; };
static inline Win decode(uint32_t rz, uint32_t rn) {
    Win w; w.sign = (rz >> 31) ? -1 : 1; int e = (rz >> 23) & 0xff; u64 m = (rz & 0x7fffff) | 0x800000;
    if (rz == 0 || rz == 0x80000000u) { w.lo = 0; w.hi = 0; w.exact = true; return w; }
    int sh = e - 150 + 26;                  // value*2^26 = m * 2^sh
    if (sh <= 0) { w.lo = w.hi = m >> (-sh); w.exact = ((m & ((1ull << (-sh)) - 1)) == 0); return w; }   // exact when representable
    w.exact = false; w.lo = m << sh; w.hi = (m << sh) + (1ll << sh) - 1;
    // At these magnitudes the RZ bin has only 2--8 integer candidates.
    // Forward-round them: symmetric RN half-ULP bounds incorrectly admit
    // ties to odd mantissas and mishandle the lower side of a binade boundary.
    i64 lo = w.hi + 1, hi = w.lo - 1;
    for (i64 v = w.lo; v <= w.hi; ++v) {
        int p = msb((u64)v), s = p - 23;
        u64 q = (u64)v >> s, r = (u64)v & ((1ull << s) - 1);
        u64 half = 1ull << (s - 1);
        q += r > half || (r == half && (q & 1));
        if (q == (1ull << 24)) { q >>= 1; ++p; }
        uint32_t bits = ((p + 127 - 26) << 23) | (q & 0x7fffff);
        if (bits == (rn & 0x7fffffff)) { if (v < lo) lo = v; hi = v; }
    }
    w.lo = lo; w.hi = hi; w.exact = lo == hi;
    return w; }
