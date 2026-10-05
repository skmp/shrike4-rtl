#include "../../fsca/tools/fsca_common.h"
#include <cmath>
int main() { const int NA = 1 << 18;
    for (int a : {1000, 5000, 20000, 100000, 200000, 262000}) { double ideal = 24592.0 * a * (double)a / pow(2, 36); u64 a2 = (u64)a * a;
        for (int j : {12, 14}) for (int i : {0, 5}) for (i64 K : {0ll, 1ll << 19}) { g_K = K; uint32_t sq = (uint32_t)(a2 >> j); uint32_t c2 = 0x6010u << i; i64 p = mult28(c2, sq); int sh = 16 - j + i; i64 T2 = sh >= 0 ? (p >> sh) : (p << (-sh)); printf("a=%6d j=%d i=%d K=%lld: sq=%08x c2=%08x p28=%lld T2=%lld ideal=%.3f exactP48>>20=%lld\n", a, j, i, (long long)K, sq, c2, (long long)p, (long long)T2, ideal, (long long)(((u64)c2 * sq) >> 20)); } }
    return 0; }
