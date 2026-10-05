// v1_fp_tb -- unit B test: every v1_fp_pkg function and the v1_fdiv unit against a C++ reference built on the host
// FPU (x86-64 SSE / FMA3, both rounding modes via fesetround, IEEE flags via fetestexcept).
//
// The reference is written from pm.txt independently of the RTL's datapath: SH-4 NaN classes and results by hand
// (sNaN = fraction MSB 1, every NaN result 0x7FBFFFFF / 0x7FF7FFFF_FFFFFFFF, V only for sNaN / invalid operations),
// DN = 1 by flushing denormal inputs to signed zero, then ONE host operation on the flushed values (float + - * /,
// sqrtss, vfmadd for FMAC, cvtsi2ss, cvtsd2ss) whose result and flags are mapped exactly as the manual's
// check_single_exception does: a host result on the denormal grid below the minimum normal (or a host zero with
// inexact) is a signed zero with U + I; overflow gives O + I (the host's own inf / max-finite result); Z, I as the
// host says.  FTRC and FCMP follow the Section 10 rules directly (host trunc / compare on the flushed value).
//
// Vectors: >= N per function per rounding mode (default 1M) from generators heavy in edge values -- zeros,
// denormals, min / max normal, infinities, both NaN kinds, short mantissas (exact results and exact ties), close
// exponents and b ~ -a (cancellation), products / quotients / conversions on the overflow and underflow borders,
// the min-normal rounding border, FMAC with c ~ -(a*b) (the product's rounding error survives alone), int32 ties
// and the FTRC range words.  The divider runs back to back at the tightest legal pitch (LAT - 2, where the proxy
// issues the next FDIV / FSQRT) and at random wider pitches; every done / busy cycle is checked.
//
//   tb [--n N] [--seed S] [--only comb|div]
#include "Vv1_fp_tb_top.h"
#include "verilated.h"
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>

static Vv1_fp_tb_top* dut;

enum : unsigned { XV = 16, XZ = 8, XO = 4, XU = 2, XI = 1 };
struct Res { uint64_t r; unsigned x; };

static const uint32_t QS = 0x7FBFFFFFu;
static const uint64_t QD = 0x7FF7FFFFFFFFFFFFull;

static inline float    fu(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t uf(float f)    { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline double   du(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline uint64_t ud(double d)   { uint64_t u; memcpy(&u, &d, 8); return u; }

// ---- SH-4 operand classes (Section 10 data_type_of, DN = 1) ------------------------------------------------------
static inline bool     s_zero(uint32_t u) { return (u & 0x7F800000u) == 0; }
static inline bool     s_inf(uint32_t u)  { return (u & 0x7FFFFFFFu) == 0x7F800000u; }
static inline bool     s_nan(uint32_t u)  { return (u & 0x7F800000u) == 0x7F800000u && (u & 0x7FFFFFu); }
static inline bool     s_snan(uint32_t u) { return s_nan(u) && (u & 0x400000u); }
static inline uint32_t s_fl(uint32_t u)   { return s_zero(u) ? (u & 0x80000000u) : u; }
static const uint64_t  DEXP = 0x7FF0000000000000ull, DFRAC = 0x000FFFFFFFFFFFFFull, DSIGN = 0x8000000000000000ull;
static inline bool     d_zero(uint64_t u) { return (u & DEXP) == 0; }
static inline bool     d_nan(uint64_t u)  { return (u & DEXP) == DEXP && (u & DFRAC); }
static inline bool     d_snan(uint64_t u) { return d_nan(u) && (u & 0x0008000000000000ull); }
static inline uint64_t d_fl(uint64_t u)   { return d_zero(u) ? (u & DSIGN) : u; }

// ---- host result -> SH-4 result + cause --------------------------------------------------------------------------
static Res fin_s(float r, int fe) {
  uint32_t u = uf(r);
  if (std::isnan(r)) {
    if (!(fe & FE_INVALID)) { fprintf(stderr, "host NaN without FE_INVALID\n"); exit(2); }
    return {QS, XV};
  }
  unsigned x = (fe & FE_DIVBYZERO) ? XZ : 0;
  if ((u & 0x7F800000u) == 0 && ((u & 0x7FFFFFFFu) || (fe & FE_INEXACT))) return {u & 0x80000000u, XU | XI};
  if (fe & FE_OVERFLOW) x |= XO | XI;
  if (fe & FE_INEXACT)  x |= XI;
  return {u, x};
}
static Res fin_d(double r, int fe) {
  uint64_t u = ud(r);
  if (std::isnan(r)) {
    if (!(fe & FE_INVALID)) { fprintf(stderr, "host NaN without FE_INVALID\n"); exit(2); }
    return {QD, XV};
  }
  unsigned x = (fe & FE_DIVBYZERO) ? XZ : 0;
  if ((u & DEXP) == 0 && ((u & ~DSIGN) || (fe & FE_INEXACT))) return {u & DSIGN, XU | XI};
  if (fe & FE_OVERFLOW) x |= XO | XI;
  if (fe & FE_INEXACT)  x |= XI;
  return {u, x};
}
// one host operation between clearing and reading the flags (operands are volatile so nothing is folded or hoisted)
#define HOST_S(expr) ([&]() -> Res { std::feclearexcept(FE_ALL_EXCEPT); volatile float  r_ = (expr); \
                                     int fe_ = std::fetestexcept(FE_ALL_EXCEPT); return fin_s(r_, fe_); }())
#define HOST_D(expr) ([&]() -> Res { std::feclearexcept(FE_ALL_EXCEPT); volatile double r_ = (expr); \
                                     int fe_ = std::fetestexcept(FE_ALL_EXCEPT); return fin_d(r_, fe_); }())

// ---- reference functions ---------------------------------------------------------------------------------------
static Res r_fadd_s(uint32_t a, uint32_t b) {
  if (s_snan(a) || s_snan(b)) return {QS, XV};
  if (s_nan(a) || s_nan(b))   return {QS, 0};
  volatile float va = fu(s_fl(a)), vb = fu(s_fl(b));
  return HOST_S(va + vb);
}
static Res r_fmul_s(uint32_t a, uint32_t b) {
  if (s_snan(a) || s_snan(b)) return {QS, XV};
  if (s_nan(a) || s_nan(b))   return {QS, 0};
  volatile float va = fu(s_fl(a)), vb = fu(s_fl(b));
  return HOST_S(va * vb);
}
static Res r_fmac_s(uint32_t a, uint32_t b, uint32_t c) {   // a * b + c
  if (s_snan(a) || s_snan(b) || s_snan(c)) return {QS, XV};
  if (s_nan(a) || s_nan(b))                return {QS, 0};
  if ((s_inf(a) && s_zero(b)) || (s_zero(a) && s_inf(b))) return {QS, XV};   // 0 * inf, even with c a qNaN
  if (s_nan(c))                            return {QS, 0};
  volatile float va = fu(s_fl(a)), vb = fu(s_fl(b)), vc = fu(s_fl(c));
  return HOST_S(__builtin_fmaf(va, vb, vc));
}
static Res r_fadd_d(uint64_t a, uint64_t b) {
  if (d_snan(a) || d_snan(b)) return {QD, XV};
  if (d_nan(a) || d_nan(b))   return {QD, 0};
  volatile double va = du(d_fl(a)), vb = du(d_fl(b));
  return HOST_D(va + vb);
}
static Res r_fmul_d(uint64_t a, uint64_t b) {
  if (d_snan(a) || d_snan(b)) return {QD, XV};
  if (d_nan(a) || d_nan(b))   return {QD, 0};
  volatile double va = du(d_fl(a)), vb = du(d_fl(b));
  return HOST_D(va * vb);
}
static Res r_fdiv_s(uint32_t a, uint32_t b) {    // a / b
  if (s_snan(a) || s_snan(b)) return {QS, XV};
  if (s_nan(a) || s_nan(b))   return {QS, 0};
  volatile float va = fu(s_fl(a)), vb = fu(s_fl(b));
  return HOST_S(va / vb);
}
static Res r_fdiv_d(uint64_t a, uint64_t b) {
  if (d_snan(a) || d_snan(b)) return {QD, XV};
  if (d_nan(a) || d_nan(b))   return {QD, 0};
  volatile double va = du(d_fl(a)), vb = du(d_fl(b));
  return HOST_D(va / vb);
}
static Res r_fsqrt_s(uint32_t a) {
  if (s_snan(a)) return {QS, XV};
  if (s_nan(a))  return {QS, 0};
  volatile float va = fu(s_fl(a));
  return HOST_S(__builtin_sqrtf(va));
}
static Res r_fsqrt_d(uint64_t a) {
  if (d_snan(a)) return {QD, XV};
  if (d_nan(a))  return {QD, 0};
  volatile double va = du(d_fl(a));
  return HOST_D(__builtin_sqrt(va));
}
static Res r_float_s(uint32_t i) { volatile int32_t vi = (int32_t)i; return HOST_S((float)vi); }
static Res r_float_d(uint32_t i) { return {ud((double)(int32_t)i), 0}; }
static Res r_ftrc_s(uint32_t a) {                // Section 10: NaN -> -MAX; out of range -> +-MAX; all with V
  if (s_nan(a)) return {0x80000000u, XV};
  double t = std::trunc((double)fu(s_fl(a)));
  if (t >= 2147483648.0)  return {0x7FFFFFFFu, XV};
  if (t < -2147483648.0)  return {0x80000000u, XV};
  return {(uint32_t)(int32_t)t, 0};
}
static Res r_ftrc_d(uint64_t a) {                // Section 10 range words compare the upper word only
  uint32_t hi = (uint32_t)(a >> 32);
  if (d_nan(a)) return {0x80000000u, XV};
  if (!(hi >> 31) && hi > 0x41DFFFFFu) return {0x7FFFFFFFu, XV};
  if ((hi >> 31) && hi > 0xC1E00000u)  return {0x80000000u, XV};
  double t = std::trunc(du(d_fl(a)));
  if (t < -2147483648.0) return {0x80000000u, 0};   // upper word 0xC1E00000, below -2^31: v1 saturates, no V
  return {(uint32_t)(int32_t)t, 0};
}
static Res r_fcnvsd(uint32_t a) {
  if (s_snan(a)) return {QD, XV};
  if (s_nan(a))  return {QD, 0};
  return {ud((double)fu(s_fl(a))), 0};
}
static Res r_fcnvds(uint64_t a) {
  if (d_snan(a)) return {QS, XV};
  if (d_nan(a))  return {QS, 0};
  volatile double va = du(d_fl(a));
  return HOST_S((float)va);
}
// FCMP: {eq, gt, v_eq, v_gt}
static unsigned r_fcmp_s(uint32_t a, uint32_t b) {
  bool veq = s_snan(a) || s_snan(b), vgt = s_nan(a) || s_nan(b), eq = false, gt = false;
  if (!vgt) { float fa = fu(s_fl(a)), fb = fu(s_fl(b)); eq = fa == fb; gt = fa > fb; }
  return (unsigned)eq << 3 | (unsigned)gt << 2 | (unsigned)veq << 1 | (unsigned)vgt;
}
static unsigned r_fcmp_d(uint64_t a, uint64_t b) {
  bool veq = d_snan(a) || d_snan(b), vgt = d_nan(a) || d_nan(b), eq = false, gt = false;
  if (!vgt) { double fa = du(d_fl(a)), fb = du(d_fl(b)); eq = fa == fb; gt = fa > fb; }
  return (unsigned)eq << 3 | (unsigned)gt << 2 | (unsigned)veq << 1 | (unsigned)vgt;
}

// ---- vector generators -----------------------------------------------------------------------------------------
struct Gen {
  std::mt19937_64 g;
  explicit Gen(uint64_t s) : g(s) {}
  uint64_t u64() { return g(); }
  uint32_t u32() { return (uint32_t)(g() >> 17); }
  int  rng(int lo, int hi) { return lo + (int)(g() % (uint64_t)(hi - lo + 1)); }   // inclusive
  bool coin()   { return g() & 1; }
  int  pick(int n) { return (int)(g() % (uint64_t)n); }

  // single-precision pieces
  uint32_t sgn() { return coin() ? 0x80000000u : 0; }
  uint32_t fr_s() { return u32() & 0x7FFFFFu; }
  uint32_t short_fr_s(int lo = 0, int hi = 12) { int k = rng(lo, hi); return k ? ((u32() & ((1u << k) - 1)) << (23 - k)) : 0; }
  uint32_t pat_fr_s() {
    int k = rng(0, 23);
    switch (pick(6)) {
      case 0:  return 0x7FFFFFu;
      case 1:  return (1u << k) - 1;                        // low ones
      case 2:  return 0x7FFFFFu & ~((1u << k) - 1);         // high ones
      case 3:  return k < 23 ? 1u << k : 0;                 // one bit
      case 4:  return 0x7FFFFFu ^ (k < 23 ? 1u << k : 0);   // one hole
      default: return (fr_s() & 0x7FFFF0u) | (u32() & 0xF);
    }
  }
  uint32_t special_s() {
    static const uint32_t T[] = {0x00000000u, 0x00000001u, 0x007FFFFFu, 0x00400000u, 0x00800000u, 0x00800001u,
                                 0x00FFFFFFu, 0x01000000u, 0x7F7FFFFFu, 0x7F7FFFFEu, 0x7F000000u, 0x7F800000u,
                                 0x3F800000u, 0x3F7FFFFFu, 0x3F800001u, 0x3FFFFFFFu, 0x40000000u, 0x4B000000u,
                                 0x4B7FFFFFu, 0x4EFFFFFFu, 0x4F000000u, 0x4F7FFFFFu, 0x34000000u, 0x33800000u};
    switch (pick(4)) {
      case 0:  return sgn() | 0x7F800000u | (u32() & 0x3FFFFFu) | 1;          // qNaN (bit 22 = 0)
      case 1:  return sgn() | 0x7FC00000u | (u32() & 0x3FFFFFu);              // sNaN (bit 22 = 1)
      case 2:  return sgn() | (u32() & 0x7FFFFFu);                            // denormal / zero
      default: return sgn() | T[pick(sizeof T / sizeof T[0])];
    }
  }
  uint32_t mk_s(uint32_t s, int e, uint32_t f) { return s | ((uint32_t)e << 23) | (f & 0x7FFFFFu); }
  uint32_t gs() {
    switch (pick(12)) {
      case 0:  return u32();
      case 1:
      case 2:  return mk_s(sgn(), rng(1, 254), fr_s());
      case 3:
      case 4:  return special_s();
      case 5:  return mk_s(sgn(), rng(1, 254), short_fr_s());
      case 6:  return mk_s(sgn(), rng(1, 254), pat_fr_s());
      case 7:  return mk_s(sgn(), coin() ? rng(1, 24) : rng(230, 254), coin() ? fr_s() : pat_fr_s());
      case 8:  return mk_s(sgn(), rng(100, 154), fr_s());
      default: return mk_s(sgn(), rng(1, 254), fr_s());
    }
  }
  uint32_t gs_norm() { uint32_t v; do v = gs(); while (s_zero(v) || (v & 0x7F800000u) == 0x7F800000u); return v; }

  // double-precision pieces
  uint64_t sgnd() { return coin() ? DSIGN : 0; }
  uint64_t fr_d() { return u64() & DFRAC; }
  uint64_t short_fr_d(int lo = 0, int hi = 27) { int k = rng(lo, hi); return k ? ((u64() & ((1ull << k) - 1)) << (52 - k)) : 0; }
  uint64_t pat_fr_d() {
    int k = rng(0, 52);
    switch (pick(6)) {
      case 0:  return DFRAC;
      case 1:  return (1ull << k) - 1;
      case 2:  return DFRAC & ~((1ull << k) - 1);
      case 3:  return k < 52 ? 1ull << k : 0;
      case 4:  return DFRAC ^ (k < 52 ? 1ull << k : 0);
      default: return (fr_d() & ~0xFull) | (u64() & 0xF);
    }
  }
  uint64_t special_d() {
    static const uint64_t T[] = {0x0000000000000000ull, 0x0000000000000001ull, 0x000FFFFFFFFFFFFFull,
                                 0x0010000000000000ull, 0x0010000000000001ull, 0x001FFFFFFFFFFFFFull,
                                 0x7FEFFFFFFFFFFFFFull, 0x7FEFFFFFFFFFFFFEull, 0x7FE0000000000000ull,
                                 0x7FF0000000000000ull, 0x3FF0000000000000ull, 0x3FEFFFFFFFFFFFFFull,
                                 0x3FF0000000000001ull, 0x3FFFFFFFFFFFFFFFull, 0x4000000000000000ull,
                                 0x41DFFFFFFFFFFFFFull, 0x41E0000000000000ull, 0x41E0000000100000ull,
                                 0x3CA0000000000000ull, 0x3C90000000000000ull};
    switch (pick(4)) {
      case 0:  return sgnd() | DEXP | (u64() & 0x0007FFFFFFFFFFFFull) | 1;    // qNaN (bit 51 = 0)
      case 1:  return sgnd() | DEXP | 0x0008000000000000ull | (u64() & 0x0007FFFFFFFFFFFFull);   // sNaN
      case 2:  return sgnd() | (u64() & DFRAC);
      default: return sgnd() | T[pick(sizeof T / sizeof T[0])];
    }
  }
  uint64_t mk_d(uint64_t s, int e, uint64_t f) { return s | ((uint64_t)e << 52) | (f & DFRAC); }
  uint64_t gd() {
    switch (pick(12)) {
      case 0:  return u64();
      case 1:
      case 2:  return mk_d(sgnd(), rng(1, 2046), fr_d());
      case 3:
      case 4:  return special_d();
      case 5:  return mk_d(sgnd(), rng(1, 2046), short_fr_d());
      case 6:  return mk_d(sgnd(), rng(1, 2046), pat_fr_d());
      case 7:  return mk_d(sgnd(), coin() ? rng(1, 55) : rng(1990, 2046), coin() ? fr_d() : pat_fr_d());
      case 8:  return mk_d(sgnd(), rng(990, 1060), fr_d());
      default: return mk_d(sgnd(), rng(1, 2046), fr_d());
    }
  }
  uint64_t gd_norm() { uint64_t v; do v = gd(); while (d_zero(v) || (v & DEXP) == DEXP); return v; }
  static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

  // ---- operand sets ------------------------------------------------------------------------------------------
  void add_s(uint32_t& a, uint32_t& b) {
    a = gs();
    switch (pick(10)) {
      case 0: case 1: case 2: b = gs(); break;
      case 3: case 4: {                                  // close exponents: cancellation, carries
        a = gs_norm(); int ea = (a >> 23) & 0xFF;
        uint32_t f = coin() ? (a & 0x7FFFFFu) ^ (u32() & ((1u << rng(0, 23)) - 1)) : fr_s();
        b = mk_s(sgn(), clampi(ea + rng(-3, 3), 1, 254), f); break;
      }
      case 5: a = gs_norm(); b = (a ^ 0x80000000u) + (uint32_t)rng(-4, 4); break;     // b ~ -a
      case 6: {                                          // rounding region: distance 20..28, short mantissas: ties
        int ea = rng(30, 254);
        a = mk_s(sgn(), ea, coin() ? short_fr_s() : pat_fr_s());
        b = mk_s(sgn(), ea - rng(20, 28), coin() ? short_fr_s() : pat_fr_s()); break;
      }
      case 7: { uint32_t s = sgn(); a = mk_s(s, rng(250, 254), coin() ? fr_s() : pat_fr_s());   // overflow
                b = mk_s(coin() ? s : sgn(), rng(250, 254), coin() ? fr_s() : pat_fr_s()); break; }
      case 8: a = mk_s(sgn(), rng(1, 3), fr_s()); b = mk_s(sgn(), rng(1, 3), fr_s()); break;   // underflow
      default: { a = gs_norm(); int ea = (a >> 23) & 0xFF;
                 b = mk_s(sgn(), clampi(ea + rng(-40, 40), 1, 254), coin() ? fr_s() : pat_fr_s()); break; }
    }
    if (coin()) std::swap(a, b);
  }
  // result exponent target T for a product of biased exponents ea + eb - 127
  void mul_target_s(int T, int& ea, int& eb) {
    int lo = std::max(1, T + 127 - 254), hi = std::min(254, T + 126);
    ea = rng(lo, hi); eb = T + 127 - ea;
  }
  void mul_s(uint32_t& a, uint32_t& b) {
    int ea, eb;
    switch (pick(10)) {
      case 0: case 1: case 2: a = gs(); b = gs(); break;
      case 3: a = mk_s(sgn(), rng(64, 190), short_fr_s(9, 14)); b = mk_s(sgn(), rng(64, 190), short_fr_s(9, 14)); break;
      case 4: case 5: mul_target_s(rng(-30, 3), ea, eb);                 // underflow border
              a = mk_s(sgn(), ea, coin() ? fr_s() : pat_fr_s()); b = mk_s(sgn(), eb, coin() ? fr_s() : pat_fr_s()); break;
      case 6: mul_target_s(rng(250, 258), ea, eb);                        // overflow border
              a = mk_s(sgn(), ea, coin() ? fr_s() : pat_fr_s()); b = mk_s(sgn(), eb, coin() ? fr_s() : pat_fr_s()); break;
      case 7: a = mk_s(sgn(), rng(1, 2), u32() & ((1u << rng(0, 23)) - 1));   // min-normal border: x * (1 - k ulp)
              b = sgn() | (coin() ? 0x3F800000u - (uint32_t)rng(1, 300) : 0x3F800000u + (uint32_t)rng(0, 3)); break;
      case 8: a = special_s(); b = gs(); break;
      default: a = gs(); b = mk_s(sgn(), rng(1, 254), 0); break;           // power of two
    }
    if (coin()) std::swap(a, b);
  }
  void mac_s(uint32_t& a, uint32_t& b, uint32_t& c) {
    int ea, eb;
    switch (pick(10)) {
      case 0: case 1: a = gs(); b = gs(); c = gs(); break;
      case 2: case 3: {                                   // c ~ -(a*b): only the product's rounding error is left
        a = mk_s(sgn(), rng(90, 164), fr_s()); b = mk_s(sgn(), rng(90, 164), coin() ? fr_s() : short_fr_s());
        double p = (double)fu(a) * (double)fu(b);
        c = uf(-(float)p) + (uint32_t)rng(-3, 3); break;
      }
      case 4: {                                           // addend within +-30 exponents of the product
        a = gs_norm(); b = gs_norm();
        int ep = clampi(((a >> 23) & 0xFF) + ((b >> 23) & 0xFF) - 127 + rng(-30, 30), 1, 254);
        c = mk_s(sgn(), ep, coin() ? fr_s() : pat_fr_s()); break;
      }
      case 5: a = mk_s(sgn(), rng(90, 164), short_fr_s()); b = mk_s(sgn(), rng(90, 164), short_fr_s());
              c = mk_s(sgn(), rng(60, 190), short_fr_s()); break;
      case 6: mul_target_s(rng(-30, 3), ea, eb);                            // tiny product, tiny addend
              a = mk_s(sgn(), ea, fr_s()); b = mk_s(sgn(), eb, fr_s());
              c = coin() ? mk_s(sgn(), rng(1, 4), fr_s()) : sgn() | (u32() & 0x7FFFFFu); break;
      case 7: mul_target_s(rng(248, 258), ea, eb);                          // overflow border
              a = mk_s(sgn(), ea, fr_s()); b = mk_s(sgn(), eb, fr_s()); c = mk_s(sgn(), rng(240, 254), fr_s()); break;
      case 8: {                                           // c ~ -(a*b) at the underflow border
        mul_target_s(rng(1, 30), ea, eb);
        a = mk_s(sgn(), ea, fr_s()); b = mk_s(sgn(), eb, fr_s());
        double p = (double)fu(a) * (double)fu(b);
        c = uf(-(float)p) + (uint32_t)rng(-3, 3); break;
      }
      default: a = gs(); b = gs(); c = gs();
               switch (pick(3)) { case 0: a = special_s(); break; case 1: b = special_s(); break; default: c = special_s(); }
    }
    if (coin()) std::swap(a, b);
  }
  void add_d(uint64_t& a, uint64_t& b) {
    a = gd();
    switch (pick(10)) {
      case 0: case 1: case 2: b = gd(); break;
      case 3: case 4: {
        a = gd_norm(); int ea = (int)((a >> 52) & 0x7FF);
        uint64_t f = coin() ? (a & DFRAC) ^ (u64() & ((1ull << rng(0, 52)) - 1)) : fr_d();
        b = mk_d(sgnd(), clampi(ea + rng(-3, 3), 1, 2046), f); break;
      }
      case 5: a = gd_norm(); b = (a ^ DSIGN) + (uint64_t)(int64_t)rng(-4, 4); break;
      case 6: {
        int ea = rng(60, 2046);
        a = mk_d(sgnd(), ea, coin() ? short_fr_d() : pat_fr_d());
        b = mk_d(sgnd(), ea - rng(49, 57), coin() ? short_fr_d() : pat_fr_d()); break;
      }
      case 7: { uint64_t s = sgnd(); a = mk_d(s, rng(2040, 2046), coin() ? fr_d() : pat_fr_d());
                b = mk_d(coin() ? s : sgnd(), rng(2040, 2046), coin() ? fr_d() : pat_fr_d()); break; }
      case 8: a = mk_d(sgnd(), rng(1, 3), fr_d()); b = mk_d(sgnd(), rng(1, 3), fr_d()); break;
      default: { a = gd_norm(); int ea = (int)((a >> 52) & 0x7FF);
                 b = mk_d(sgnd(), clampi(ea + rng(-70, 70), 1, 2046), coin() ? fr_d() : pat_fr_d()); break; }
    }
    if (coin()) std::swap(a, b);
  }
  void mul_target_d(int T, int& ea, int& eb) {
    int lo = std::max(1, T + 1023 - 2046), hi = std::min(2046, T + 1022);
    ea = rng(lo, hi); eb = T + 1023 - ea;
  }
  void mul_d(uint64_t& a, uint64_t& b) {
    int ea, eb;
    switch (pick(10)) {
      case 0: case 1: case 2: a = gd(); b = gd(); break;
      case 3: a = mk_d(sgnd(), rng(700, 1300), short_fr_d(24, 29)); b = mk_d(sgnd(), rng(700, 1300), short_fr_d(24, 29)); break;
      case 4: case 5: mul_target_d(rng(-60, 3), ea, eb);
              a = mk_d(sgnd(), ea, coin() ? fr_d() : pat_fr_d()); b = mk_d(sgnd(), eb, coin() ? fr_d() : pat_fr_d()); break;
      case 6: mul_target_d(rng(2042, 2050), ea, eb);
              a = mk_d(sgnd(), ea, coin() ? fr_d() : pat_fr_d()); b = mk_d(sgnd(), eb, coin() ? fr_d() : pat_fr_d()); break;
      case 7: a = mk_d(sgnd(), rng(1, 2), u64() & ((1ull << rng(0, 52)) - 1));
              b = sgnd() | (coin() ? 0x3FF0000000000000ull - (uint64_t)rng(1, 300) : 0x3FF0000000000000ull + (uint64_t)rng(0, 3)); break;
      case 8: a = special_d(); b = gd(); break;
      default: a = gd(); b = mk_d(sgnd(), rng(1, 2046), 0); break;
    }
    if (coin()) std::swap(a, b);
  }
  void cmp_s(uint32_t& a, uint32_t& b) {
    a = gs();
    switch (pick(6)) {
      case 0: b = gs(); break;
      case 1: b = a; break;
      case 2: b = a + (uint32_t)rng(-2, 2); break;
      case 3: b = a ^ 0x80000000u; break;
      case 4: a = sgn() | (u32() & (coin() ? 0x7FFFFFu : 0)); b = sgn() | (u32() & (coin() ? 0x7FFFFFu : 0)); break;
      default: b = special_s(); break;
    }
    if (coin()) std::swap(a, b);
  }
  void cmp_d(uint64_t& a, uint64_t& b) {
    a = gd();
    switch (pick(6)) {
      case 0: b = gd(); break;
      case 1: b = a; break;
      case 2: b = a + (uint64_t)(int64_t)rng(-2, 2); break;
      case 3: b = a ^ DSIGN; break;
      case 4: a = sgnd() | (u64() & (coin() ? DFRAC : 0)); b = sgnd() | (u64() & (coin() ? DFRAC : 0)); break;
      default: b = special_d(); break;
    }
    if (coin()) std::swap(a, b);
  }
  uint32_t gint() {
    static const uint32_t T[] = {0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x80000001u, 0x01000000u,
                                 0x01000001u, 0x01000003u, 0x00FFFFFFu, 0x7FFFFFC0u, 0x7FFFFF80u, 0x7FFFFF7Fu};
    switch (pick(6)) {
      case 0:  return u32();
      case 1:  return (uint32_t)rng(-1000, 1000);
      case 2: { int k = rng(1, 31); uint32_t v = (u32() & ((1u << k) - 1)) | (1u << (k - 1)); return coin() ? 0u - v : v; }
      case 3: { uint32_t v = ((u32() & 0xFFFFFFu) | 0x1000000u | 1u) << rng(0, 6); return coin() ? 0u - v : v; }   // 25-bit ties
      case 4: { uint32_t v = ((u32() & 0xFFFFFFu) | 0x1000000u) << rng(0, 6); return coin() ? 0u - v : v; }
      default: { uint32_t v = T[pick(sizeof T / sizeof T[0])]; return coin() ? v : 0u - v; }
    }
  }
  uint32_t ftrc_in_s() {
    switch (pick(5)) {
      case 0:  return gs();
      case 1:  return mk_s(sgn(), rng(155, 159), coin() ? fr_s() : pat_fr_s());          // around 2^31
      case 2:  return mk_s(sgn(), rng(120, 160), fr_s());
      case 3:  return mk_s(sgn(), rng(127, 157), short_fr_s());                         // exact integers
      default: { static const uint32_t T[] = {0x4EFFFFFFu, 0x4F000000u, 0xCF000000u, 0xCF000001u, 0x7F800000u,
                                              0xFF800000u, 0x3F7FFFFFu, 0xBF7FFFFFu, 0x3F800000u, 0xBF800000u,
                                              0x7F800001u, 0xFFC00000u, 0x7FFFFFFFu, 0x80000000u, 0x00000001u};
                 return coin() ? T[pick(sizeof T / sizeof T[0])] : special_s(); }
    }
  }
  uint64_t ftrc_in_d() {
    static const uint32_t H[] = {0x41DFFFFFu, 0x41E00000u, 0xC1E00000u, 0xC1E00001u, 0xC1DFFFFFu, 0x7FF00000u,
                                 0xFFF00000u, 0x3FEFFFFFu, 0xBFF00000u, 0x41D00000u, 0xC1D00000u};
    switch (pick(5)) {
      case 0:  return gd();
      case 1:  return ((uint64_t)H[pick(sizeof H / sizeof H[0])] << 32) | (coin() ? u32() : (uint32_t)rng(0, 3) * 0x100000u);
      case 2:  return mk_d(sgnd(), rng(1015, 1056), fr_d());
      case 3:  return mk_d(sgnd(), rng(1023, 1053), short_fr_d());
      default: return special_d();
    }
  }
  uint64_t cnvds_in() {
    switch (pick(7)) {
      case 0:  return gd();
      case 1:  return mk_d(sgnd(), rng(1023 - 127 - 30, 1023 - 125), coin() ? fr_d() : pat_fr_d());   // underflow
      case 2:  return mk_d(sgnd(), rng(1023 + 125, 1023 + 129), coin() ? fr_d() : pat_fr_d());        // overflow
      case 3: {                                                        // ties / near ties below single precision
        static const uint64_t L[] = {0x10000000ull, 0x0FFFFFFFull, 0x10000001ull, 0x1FFFFFFFull, 0x00000001ull, 0};
        return mk_d(sgnd(), rng(1023 - 140, 1023 + 130), (fr_d() & ~0x1FFFFFFFull) | L[pick(6)]);
      }
      case 4:  return mk_d(sgnd(), 1023 - 127, (DFRAC & ~0x1FFFFFFFull) | (u64() & 0x1FFFFFFFull));   // min-normal border
      case 5:  return special_d();
      default: return mk_d(sgnd(), rng(1023 - 126, 1023 + 127), fr_d());
    }
  }
  // divider operands
  void div_s(uint32_t& a, uint32_t& b) {
    switch (pick(10)) {
      case 9: a = mk_s(sgn(), rng(1, 2), u32() & ((1u << rng(0, 23)) - 1));   // quotient on the min-normal border
              b = sgn() | (0x3F800000u + (uint32_t)rng(0, 300)); break;
      case 0: case 1: a = gs(); b = gs(); break;
      case 2: {                                          // exact quotients: a = q * b with 12-bit q and b
        uint32_t q = mk_s(0, rng(110, 144), short_fr_s() & 0x7FF000u), d = mk_s(sgn(), rng(90, 164), short_fr_s() & 0x7FF000u);
        b = d; a = uf(fu(q) * fu(d)) ^ (coin() ? 0x80000000u : 0); break;
      }
      case 3: { int ea = rng(1, 128), eb = clampi(ea + 127 - rng(-30, 3), 1, 254);        // underflow border
                a = mk_s(sgn(), ea, fr_s()); b = mk_s(sgn(), eb, fr_s()); break; }
      case 4: { int eb = rng(1, 128), ea = clampi(eb - 127 + rng(250, 258), 1, 254);      // overflow border
                a = mk_s(sgn(), ea, fr_s()); b = mk_s(sgn(), eb, fr_s()); break; }
      case 5: { a = gs_norm(); uint32_t f = (a & 0x7FFFFFu) + (uint32_t)rng(-3, 3);       // mantissas close: a ~ b
                b = mk_s(sgn(), rng(1, 254), f); break; }
      case 6: a = special_s(); b = gs(); if (coin()) std::swap(a, b); break;
      case 7: a = mk_s(sgn(), rng(1, 254), pat_fr_s()); b = mk_s(sgn(), rng(1, 254), pat_fr_s()); break;
      default: a = mk_s(sgn(), rng(1, 254), fr_s()); b = mk_s(sgn(), rng(1, 254), fr_s()); break;
    }
  }
  uint32_t sqrt_s() {
    switch (pick(6)) {
      case 0: return gs();
      case 1: { uint32_t q = mk_s(0, rng(64, 190), short_fr_s() & 0x7FF000u); return uf(fu(q) * fu(q)); }   // squares
      case 2: { uint32_t q = mk_s(0, rng(64, 190), short_fr_s() & 0x7FF000u); return uf(fu(q) * fu(q)) + (uint32_t)rng(-2, 2); }
      case 3: return special_s();
      case 4: return mk_s(0, rng(1, 254), pat_fr_s());
      default: return mk_s(0, rng(1, 254), fr_s());
    }
  }
  void div_d(uint64_t& a, uint64_t& b) {
    switch (pick(10)) {
      case 9: a = mk_d(sgnd(), rng(1, 2), u64() & ((1ull << rng(0, 52)) - 1));
              b = sgnd() | (0x3FF0000000000000ull + (uint64_t)rng(0, 300)); break;
      case 0: case 1: a = gd(); b = gd(); break;
      case 2: {
        uint64_t q = mk_d(0, rng(990, 1060), short_fr_d() & ~((1ull << 26) - 1)), d = mk_d(sgnd(), rng(700, 1300), short_fr_d() & ~((1ull << 26) - 1));
        b = d; a = ud(du(q) * du(d)) ^ (coin() ? DSIGN : 0); break;
      }
      case 3: { int ea = rng(1, 1024), eb = clampi(ea + 1023 - rng(-60, 3), 1, 2046);
                a = mk_d(sgnd(), ea, fr_d()); b = mk_d(sgnd(), eb, fr_d()); break; }
      case 4: { int eb = rng(1, 1024), ea = clampi(eb - 1023 + rng(2042, 2050), 1, 2046);
                a = mk_d(sgnd(), ea, fr_d()); b = mk_d(sgnd(), eb, fr_d()); break; }
      case 5: { a = gd_norm(); uint64_t f = (a & DFRAC) + (uint64_t)(int64_t)rng(-3, 3);
                b = mk_d(sgnd(), rng(1, 2046), f); break; }
      case 6: a = special_d(); b = gd(); if (coin()) std::swap(a, b); break;
      case 7: a = mk_d(sgnd(), rng(1, 2046), pat_fr_d()); b = mk_d(sgnd(), rng(1, 2046), pat_fr_d()); break;
      default: a = mk_d(sgnd(), rng(1, 2046), fr_d()); b = mk_d(sgnd(), rng(1, 2046), fr_d()); break;
    }
  }
  uint64_t sqrt_d() {
    switch (pick(6)) {
      case 0: return gd();
      case 1: { uint64_t q = mk_d(0, rng(600, 1400), short_fr_d() & ~((1ull << 26) - 1)); return ud(du(q) * du(q)); }
      case 2: { uint64_t q = mk_d(0, rng(600, 1400), short_fr_d() & ~((1ull << 26) - 1)); return ud(du(q) * du(q)) + (uint64_t)(int64_t)rng(-2, 2); }
      case 3: return special_d();
      case 4: return mk_d(0, rng(1, 2046), pat_fr_d());
      default: return mk_d(0, rng(1, 2046), fr_d());
    }
  }
};

// ---- coverage: exact RN ties, from an exact integer model of the operation (RN pass, normal-range results) ------
// value = (-1)^s * m * 2^e; a tie = the bits below the kept 24 / 53 significant bits are exactly 100..0.
typedef unsigned __int128 u128;
struct Term { bool s; int e; u128 m; };
static bool dec_s(uint32_t u, Term& t) {
  if (s_zero(u) || (u & 0x7F800000u) == 0x7F800000u) return false;
  t = {(u >> 31) != 0, (int)((u >> 23) & 0xFF) - 150, (u128)((u & 0x7FFFFFu) | 0x800000u)}; return true;
}
static bool dec_d(uint64_t u, Term& t) {
  if (d_zero(u) || (u & DEXP) == DEXP) return false;
  t = {(u >> 63) != 0, (int)((u >> 52) & 0x7FF) - 1075, (u128)((u & DFRAC) | (1ull << 52))}; return true;
}
static int msb128(u128 v) { uint64_t h = (uint64_t)(v >> 64); return h ? 127 - __builtin_clzll(h) : 63 - __builtin_clzll((uint64_t)v); }
static bool tie_of(u128 v, int keep) {
  if (v == 0) return false;
  int p = msb128(v);
  if (p < keep) return false;
  int sh = p + 1 - keep;
  return (v & ((((u128)1) << sh) - 1)) == (((u128)1) << (sh - 1));
}
static bool sum_tie(Term x, Term y, int keep) {
  if (x.m == 0) return tie_of(y.m, keep);
  if (x.e < y.e) std::swap(x, y);
  int d = x.e - y.e;
  if (d + msb128(x.m) > 125) return false;          // too far apart to be a tie
  u128 X = x.m << d, Y = y.m;
  return tie_of(x.s == y.s ? X + Y : (X > Y ? X - Y : Y - X), keep);
}
static bool tie_case(int op, uint64_t a, uint64_t b, uint32_t c);   // below, after the opcode list

// ---- bookkeeping -------------------------------------------------------------------------------------------------
struct Stat {
  const char* name; long n = 0, bad = 0, ties = 0, minrnd = 0; long cause[5] = {0, 0, 0, 0, 0};
  void add(unsigned x) { for (int k = 0; k < 5; k++) if (x >> (4 - k) & 1) cause[k]++; }
};
static const int MAXPRINT = 8;
static void report(const Stat& s, const char* mode) {
  printf("  %-8s %-2s  %9ld vectors  %6ld mismatches   causes V %-7ld Z %-7ld O %-7ld U %-7ld I %-7ld  ties %-6ld =minnorm+I %ld\n",
         s.name, mode, s.n, s.bad, s.cause[0], s.cause[1], s.cause[2], s.cause[3], s.cause[4], s.ties, s.minrnd);
}

enum { OP_FADD_S, OP_FMUL_S, OP_FMAC_S, OP_FADD_D, OP_FMUL_D, OP_FCMP_S, OP_FCMP_D, OP_FLOAT_S, OP_FLOAT_D,
       OP_FTRC_S, OP_FTRC_D, OP_FCNVSD, OP_FCNVDS, OP_NCOMB };
static const char* OPN[] = {"fadd_s", "fmul_s", "fmac_s", "fadd_d", "fmul_d", "fcmp_s", "fcmp_d", "float_s",
                            "float_d", "ftrc_s", "ftrc_d", "fcnvsd", "fcnvds"};

static bool tie_case(int op, uint64_t a, uint64_t b, uint32_t c) {
  Term x, y, z;
  switch (op) {
    case OP_FADD_S: return dec_s((uint32_t)a, x) && dec_s((uint32_t)b, y) && sum_tie(x, y, 24);
    case OP_FMUL_S: return dec_s((uint32_t)a, x) && dec_s((uint32_t)b, y) && tie_of(x.m * y.m, 24);
    case OP_FMAC_S:
      if (!dec_s((uint32_t)a, x) || !dec_s((uint32_t)b, y)) return false;
      z = {false, 0, 0};
      if (!s_zero(c) && !dec_s(c, z)) return false;
      return sum_tie({x.s != y.s, x.e + y.e, x.m * y.m}, z, 24);
    case OP_FADD_D: return dec_d(a, x) && dec_d(b, y) && sum_tie(x, y, 53);
    case OP_FMUL_D: return dec_d(a, x) && dec_d(b, y) && tie_of(x.m * y.m, 53);
    case OP_FLOAT_S: { int32_t i = (int32_t)a; return tie_of((u128)(i < 0 ? -(int64_t)i : (int64_t)i), 24); }
    case OP_FCNVDS: return dec_d(a, x) && tie_of(x.m, 24);
    default: return false;
  }
}

static long run_comb(long N, uint64_t seed, bool rz) {
  Gen G(seed);
  long total_bad = 0;
  for (int op = 0; op < OP_NCOMB; op++) {
    Stat st; st.name = OPN[op];
    for (long k = 0; k < N; k++) {
      uint64_t a = 0, b = 0; uint32_t c = 0, a32, b32, c32;
      Res ref{0, 0}; unsigned rcmp = 0; bool is_cmp = false;
      switch (op) {
        case OP_FADD_S:  G.add_s(a32, b32); a = a32; b = b32; ref = r_fadd_s(a32, b32); break;
        case OP_FMUL_S:  G.mul_s(a32, b32); a = a32; b = b32; ref = r_fmul_s(a32, b32); break;
        case OP_FMAC_S:  G.mac_s(a32, b32, c32); a = a32; b = b32; c = c32; ref = r_fmac_s(a32, b32, c32); break;
        case OP_FADD_D:  G.add_d(a, b); ref = r_fadd_d(a, b); break;
        case OP_FMUL_D:  G.mul_d(a, b); ref = r_fmul_d(a, b); break;
        case OP_FCMP_S:  G.cmp_s(a32, b32); a = a32; b = b32; rcmp = r_fcmp_s(a32, b32); is_cmp = true; break;
        case OP_FCMP_D:  G.cmp_d(a, b); rcmp = r_fcmp_d(a, b); is_cmp = true; break;
        case OP_FLOAT_S: a = G.gint(); ref = r_float_s((uint32_t)a); break;
        case OP_FLOAT_D: a = G.gint(); ref = r_float_d((uint32_t)a); break;
        case OP_FTRC_S:  a = G.ftrc_in_s(); ref = r_ftrc_s((uint32_t)a); break;
        case OP_FTRC_D:  a = G.ftrc_in_d(); ref = r_ftrc_d(a); break;
        case OP_FCNVSD:  a = G.gs(); ref = r_fcnvsd((uint32_t)a); break;
        case OP_FCNVDS:  a = G.cnvds_in(); ref = r_fcnvds(a); break;
      }
      dut->op = op; dut->rz = rz; dut->a = a; dut->b = b; dut->c = c;
      dut->eval();
      st.n++;
      bool bad;
      if (is_cmp) {
        unsigned got = dut->cmp;
        bad = got != rcmp;
        st.add((rcmp & 3) ? XV : 0);
        if (bad && st.bad < MAXPRINT)
          printf("MISMATCH %s %s a=%016llx b=%016llx  got eq%u gt%u veq%u vgt%u  exp eq%u gt%u veq%u vgt%u\n", OPN[op],
                 rz ? "RZ" : "RN", (unsigned long long)a, (unsigned long long)b, got >> 3 & 1, got >> 2 & 1,
                 got >> 1 & 1, got & 1, rcmp >> 3 & 1, rcmp >> 2 & 1, rcmp >> 1 & 1, rcmp & 1);
      } else {
        uint64_t gr = dut->r; unsigned gx = dut->x;
        bad = gr != ref.r || gx != ref.x;
        st.add(ref.x);
        bool dres = op == OP_FADD_D || op == OP_FMUL_D || op == OP_FLOAT_D || op == OP_FCNVSD;
        uint64_t mag = dres ? ref.r & ~DSIGN : ref.r & 0x7FFFFFFFu, emask = dres ? DEXP : 0x7F800000u;
        if (mag == (dres ? 0x0010000000000000ull : 0x00800000u) && ref.x == XI) st.minrnd++;   // rounded up to min normal
        if (!rz && (mag & emask) != 0 && (mag & emask) != emask && tie_case(op, a, b, c)) st.ties++;
        if (bad && st.bad < MAXPRINT)
          printf("MISMATCH %s %s a=%016llx b=%016llx c=%08x  got %016llx x=%02x  exp %016llx x=%02x\n", OPN[op],
                 rz ? "RZ" : "RN", (unsigned long long)a, (unsigned long long)b, c, (unsigned long long)gr, gx,
                 (unsigned long long)ref.r, ref.x);
      }
      if (bad) st.bad++;
    }
    report(st, rz ? "RZ" : "RN");
    total_bad += st.bad;
  }
  return total_bad;
}

// ---- divider: back-to-back stream with exact done / busy timing ------------------------------------------------
static long g_cycle = 0;
static void tick() { dut->clk = 1; dut->eval(); dut->clk = 0; dut->eval(); g_cycle++; }

static long run_div(long N, uint64_t seed, bool rz) {
  static const int LAT[4] = {12, 11, 25, 24};    // {dbl, sqrt}: FDIV, FSQRT, FDIV DR, FSQRT DR
  static const char* NM[4] = {"fdiv_s", "fsqrt_s", "fdiv_d", "fsqrt_d"};
  Gen G(seed ^ 0x9E3779B97F4A7C15ull);
  long total_bad = 0;
  for (int kind = 0; kind < 4; kind++) {
    Stat st; st.name = NM[kind];
    long timing_bad = 0, pitch_min = 0;
    struct P { long start, due; uint64_t a, b; Res ref; };
    std::deque<P> q;
    long issued = 0, next_ok = g_cycle, busy_until = -1;   // busy expected in [start+1, start+LAT-3]
    long busy_from = -1;
    const int L = LAT[kind];
    const bool dbl = kind >= 2, sq = kind & 1;
    while (issued < N || !q.empty()) {
      bool go = false;
      if (issued < N && g_cycle >= next_ok) go = G.pick(4) != 0 || g_cycle >= next_ok + 3;   // mostly tightest pitch
      dut->d_start = go; dut->d_sqrt = sq; dut->d_dbl = dbl; dut->d_rz = rz;
      if (go) {
        P p; p.start = g_cycle; p.due = g_cycle + L - 1;
        if (dbl) { if (sq) { p.a = G.sqrt_d(); p.b = G.gd(); p.ref = r_fsqrt_d(p.a); }
                   else    { G.div_d(p.a, p.b); p.ref = r_fdiv_d(p.a, p.b); } }
        else     { uint32_t a32, b32;
                   if (sq) { a32 = G.sqrt_s(); b32 = G.u32(); p.ref = r_fsqrt_s(a32); }
                   else    { G.div_s(a32, b32); p.ref = r_fdiv_s(a32, b32); }
                   p.a = a32 | ((uint64_t)G.u32() << 32); p.b = b32 | ((uint64_t)G.u32() << 32); }   // junk above a single
        dut->d_a = p.a; dut->d_b = p.b;
        if (g_cycle == next_ok) pitch_min++;
        q.push_back(p); issued++;
        next_ok = g_cycle + L - 2;                       // set_div: the next FDIV / FSQRT may issue here
      } else {
        dut->d_a = G.u64(); dut->d_b = G.u64();          // inputs are don't-care without start
      }
      dut->eval();
      // outputs of this cycle (registered: they reflect the state after the previous edge)
      bool exp_busy = busy_from >= 0 && g_cycle >= busy_from && g_cycle <= busy_until;
      if ((bool)dut->d_busy != exp_busy) {
        if (timing_bad < MAXPRINT) printf("TIMING %s cycle %ld: busy=%d expected %d\n", NM[kind], g_cycle, dut->d_busy, exp_busy);
        timing_bad++;
      }
      bool exp_done = !q.empty() && q.front().due == g_cycle;
      if ((bool)dut->d_done != exp_done) {
        if (timing_bad < MAXPRINT) printf("TIMING %s cycle %ld: done=%d expected %d\n", NM[kind], g_cycle, dut->d_done, exp_done);
        timing_bad++;
      }
      if (exp_done) {
        P p = q.front(); q.pop_front();
        uint64_t gr = dut->d_r; unsigned gx = dut->d_x;
        st.n++; st.add(p.ref.x);   // (quotients and roots of p-bit numbers are never exact ties: ties stays 0)
        uint64_t mag = dbl ? p.ref.r & ~DSIGN : p.ref.r & 0x7FFFFFFFu;
        if (mag == (dbl ? 0x0010000000000000ull : 0x00800000u) && p.ref.x == XI) st.minrnd++;
        if (gr != p.ref.r || gx != p.ref.x) {
          if (st.bad < MAXPRINT)
            printf("MISMATCH %s %s a=%016llx b=%016llx  got %016llx x=%02x  exp %016llx x=%02x\n", NM[kind],
                   rz ? "RZ" : "RN", (unsigned long long)(dbl ? p.a : p.a & 0xFFFFFFFFu),
                   (unsigned long long)(dbl ? p.b : p.b & 0xFFFFFFFFu), (unsigned long long)gr, gx,
                   (unsigned long long)p.ref.r, p.ref.x);
          st.bad++;
        }
      }
      if (go) { busy_from = g_cycle + 1; busy_until = g_cycle + L - 3; }
      tick();
    }
    dut->d_start = 0;
    report(st, rz ? "RZ" : "RN");
    printf("  %-8s %-2s  latency %d: done at start+%d, busy start+1..+%d, %ld of %ld starts at the tightest pitch (%d), "
           "%ld timing errors\n", NM[kind], rz ? "RZ" : "RN", L, L - 1, L - 3, pitch_min, issued, L - 2, timing_bad);
    total_bad += st.bad + timing_bad;
  }
  return total_bad;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  long N = 1000000; uint64_t seed = 1; std::string only;
  for (int i = 1; i < argc; i++) {
    std::string s = argv[i];
    if (s == "--n" && i + 1 < argc) N = atol(argv[++i]);
    else if (s == "--seed" && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
    else if (s == "--only" && i + 1 < argc) only = argv[++i];
  }
  dut = new Vv1_fp_tb_top;
  dut->clk = 0; dut->rst = 1; dut->d_start = 0; dut->eval();
  tick(); tick();
  dut->rst = 0; dut->eval();
  long bad = 0;
  for (int rz = 0; rz < 2; rz++) {
    if (std::fesetround(rz ? FE_TOWARDZERO : FE_TONEAREST)) { fprintf(stderr, "fesetround failed\n"); return 2; }
    if (only.empty() || only == "comb") { printf("v1_fp functions, %s, %ld vectors each:\n", rz ? "RZ" : "RN", N); bad += run_comb(N, seed + rz, rz); }
    if (only.empty() || only == "div")  { printf("v1_fdiv, %s, %ld operations each:\n", rz ? "RZ" : "RN", N); bad += run_div(N, seed + 7 + rz, rz); }
  }
  std::fesetround(FE_TONEAREST);
  printf("%s: %ld mismatches\n", bad ? "FAIL" : "PASS", bad);
  delete dut;
  return bad ? 1 : 0;
}
