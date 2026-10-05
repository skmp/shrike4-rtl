// Exhaustive inverse-conversion check over the FSCA internal magnitude range.
#include "fsca_common.h"
#include "../archive/fsca_ref.h"
#include <cassert>
static inline Win decode_legacy(uint32_t rz, uint32_t rn) {
    Win w; w.sign = (rz >> 31) ? -1 : 1; int e = (rz >> 23) & 0xff; u64 m = (rz & 0x7fffff) | 0x800000;
    if (rz == 0 || rz == 0x80000000u) { w.lo = 0; w.hi = 0; w.exact = true; return w; }
    int sh = e - 150 + 26;                  // value*2^26 = m * 2^sh
    if (sh <= 0) { w.lo = w.hi = m >> (-sh); w.exact = ((m & ((1ull << (-sh)) - 1)) == 0); return w; }   // exact when representable
    w.exact = false; w.lo = m << sh; w.hi = (m << sh) + (1ll << sh) - 1;
    // RN narrows: |V| rounds to mn*2^shn
    int en = (rn >> 23) & 0xff; u64 mn = (rn & 0x7fffff) | 0x800000; int shn = en - 150 + 26; if (shn > 0) { i64 c = (i64)(mn << shn), h = 1ll << (shn - 1); i64 lo2 = c - h, hi2 = c + h; if (lo2 > w.lo) w.lo = lo2; if (hi2 < w.hi) w.hi = hi2; }
    return w; }

int main() {
    unsigned failures = 0;
    for (i64 v = 0; v <= (1ll << 26); ++v) {
        uint32_t rz = fsca_to_float(v, 1), rn = fsca_to_float(v, 0);
        Win w = decode(rz, rn);
        if (v < w.lo || v > w.hi) ++failures;
        for (i64 u = w.lo; u <= w.hi; ++u)
            if (fsca_to_float(u, 1) != rz || fsca_to_float(u, 0) != rn) ++failures;
    }
    printf("exhaustive magnitudes 0..2^26: %u inverse-conversion failures\n", failures);
    Fsca f; if (!load_fsca(f)) return 2;
    int changed = 0, singleton = 0, empty = 0, oldbad = 0, newbad = 0;
    int singleton_nodes[64] = {};
    for (int i = 0; i < 65536; ++i) {
        Win old = decode_legacy(f.srz[i], f.srn[i]), w = decode(f.srz[i], f.srn[i]);
        changed += old.lo != w.lo || old.hi != w.hi;
        singleton += w.exact; empty += w.lo > w.hi;
        if (w.exact) ++singleton_nodes[((i+512)>>10)&63];
        i64 v = fsca_internal(i); if (v < 0) v = -v;
        oldbad += v < old.lo || v > old.hi;
        newbad += v < w.lo || v > w.hi;
    }
    printf("hardware sin: %d changed windows, %d singleton windows, %d empty\n",changed,singleton,empty);
    printf("reference window violations: legacy %d, corrected %d\n",oldbad,newbad);
    printf("singleton counts nodes 0..16:");
    for(int n=0;n<=16;++n) printf(" %d:%d",n,singleton_nodes[n]);
    puts("");
    return failures || empty;
}
