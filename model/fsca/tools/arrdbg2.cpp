#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { g_K = 0; i64 A2 = 0xf79b60; for (int a : {3, 100, 300, 511}) { uint32_t sq = (uint32_t)(a * a) << 5; i64 p0 = mult28((uint32_t)A2, sq), p1 = mult28(sq, (uint32_t)A2); printf("a=%d exact=%.4f arr(rec=C2)=%lld arr(rec=sq)=%lld  (p28: %lld %lld)\n", a, (double)A2 * a * a / pow(2, 29), (long long)(p0 >> 14), (long long)(p1 >> 14), (long long)p0, (long long)p1); } return 0; }
