// f3_field.h -- an extended set of sine-field / alignment rules for the square's table entry.  Each is ONE
// rule applied to all 17 nodes; none has a per-node degree of freedom.  Rules 0..8 are e_final.h's, repeated
// here so the two sweeps are comparable; 9..21 widen the family in the directions e_final.h left out
// (truncation at other widths, round-half-up instead of round-half-even, fixed-point fields at other scales,
// and reusing the 26-bit C0 table as the operand).
// The pairing of field width with alignment is forced: the square term's value must stay on the same grid,
// so a field of sin*2^(b+k) is aligned by u = b + 7 + k.
#pragma once
#include "base2.h"
#include <algorithm>
static const int NSV2 = 22;
static const char* SV2[NSV2] = {
 "RN23 normalised, u=30+k (published)", "trunc23 normalised, u=30+k", "RN24 normalised, u=31+k",
 "trunc24 normalised, u=31+k", "RN25 normalised, u=32+k", "fixed RN(sin*2^24), u=31",
 "fixed RN(sin*2^23), u=30", "the 26-bit C0 as the operand, u=33", "RN22 normalised, u=29+k",
 "trunc22 normalised, u=29+k", "RN21 normalised, u=28+k", "trunc21 normalised, u=28+k",
 "fixed RN(sin*2^22), u=29", "fixed RN(sin*2^25), u=32", "fixed trunc(sin*2^24), u=31",
 "fixed trunc(sin*2^23), u=30", "RN26 normalised, u=33+k", "trunc25 normalised, u=32+k",
 "half-up 23 normalised, u=30+k", "half-up 24 normalised, u=31+k", "C0>>2 as the operand, u=31",
 "trunc20 normalised, u=27+k" };
static inline i64 rn_half_up(i128 v, int sh) { i128 num = sh >= 0 ? v << sh : v >> (-sh);
    i128 q = num / QONE, r = num % QONE; if (r * 2 >= QONE) ++q; return (i64)q; }
static const i64 C0T[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void fin_field2(int sv, int n, i64& S, int& u) {
    int k = TB[n].u2 - 30; i128 s = sin_(PI_() * n / 32);
    auto rn = [&](int sh) { return rn_scale(s, sh); };
    auto tr = [&](int sh) { return (i64)((s << sh) / QONE); };
    auto cl = [&](i64 v, int b) { return std::min(v, (i64)((1ll << b) - 1)); };
    switch (sv) {
      case 0:  S = cl(rn(23 + k), 23); u = 30 + k; break;
      case 1:  S = cl(tr(23 + k), 23); u = 30 + k; break;
      case 2:  S = cl(rn(24 + k), 24); u = 31 + k; break;
      case 3:  S = cl(tr(24 + k), 24); u = 31 + k; break;
      case 4:  S = cl(rn(25 + k), 25); u = 32 + k; break;
      case 5:  S = rn(24); u = 31; break;
      case 6:  S = rn(23); u = 30; break;
      case 7:  S = cl(rn(26), 26); u = 33; break;
      case 8:  S = cl(rn(22 + k), 22); u = 29 + k; break;
      case 9:  S = cl(tr(22 + k), 22); u = 29 + k; break;
      case 10: S = cl(rn(21 + k), 21); u = 28 + k; break;
      case 11: S = cl(tr(21 + k), 21); u = 28 + k; break;
      case 12: S = rn(22); u = 29; break;
      case 13: S = rn(25); u = 32; break;
      case 14: S = tr(24); u = 31; break;
      case 15: S = tr(23); u = 30; break;
      case 16: S = cl(rn(26 + k), 26); u = 33 + k; break;
      case 17: S = cl(tr(25 + k), 25); u = 32 + k; break;
      case 18: S = cl(rn_half_up(s, 23 + k), 23); u = 30 + k; break;
      case 19: S = cl(rn_half_up(s, 24 + k), 24); u = 31 + k; break;
      case 20: S = C0T[n] >> 2; u = 31; break;
      default: S = cl(tr(20 + k), 20); u = 27 + k; break;
    }
    if (n == 0) S = 0;
}
