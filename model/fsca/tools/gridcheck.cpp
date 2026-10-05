#include "fsca_common.h"
int main() { Fsca F; load_fsca(F); int small = 0, finer = 0, rnrzdiff_small = 0; int minexp = 999; int tz_hist[8] = {};
    for (int i = 0; i < 65536; i++) { uint32_t v = F.srn[i] & 0x7fffffff; if (!v) continue; int e = (v >> 23) & 0xff; u64 m = (v & 0x7fffff) | 0x800000; int sh = e - 150 + 26;   // value * 2^26 = m * 2^sh
        if (sh <= 0) { small++; if (m & ((1ull << (-sh)) - 1)) finer++; if (F.srn[i] != F.srz[i]) rnrzdiff_small++; } if (e < minexp) minexp = e; }
    printf("values with |sin| < 0.25: %d; of those with bits finer than 2^-26: %d; RN != RZ among them: %d; smallest exponent %d\n", small, finer, rnrzdiff_small, minexp);
    // same for cos
    int small2 = 0, finer2 = 0; for (int i = 0; i < 65536; i++) { uint32_t v = F.crn[i] & 0x7fffffff; if (!v) continue; int e = (v >> 23) & 0xff; u64 m = (v & 0x7fffff) | 0x800000; int sh = e - 150 + 26; if (sh <= 0) { small2++; if (m & ((1ull << (-sh)) - 1)) finer2++; } } printf("cos: %d small, %d finer\n", small2, finer2); return 0; }
