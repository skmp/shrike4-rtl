#include "fsca_common.h"
#include <cmath>
int main() { g_K = 1ll << 19; double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37));
    for (int cfg = 0; cfg < 3; cfg++) { double mn = 9, mx = -9, sum = 0; for (int a = 1; a <= 511; a++) { double d; if (cfg < 2) { Op c1 { (uint32_t)m1, -14 }, ao = op_fixed(a, 15); i64 p = cfg == 0 ? mult28(ao.m, c1.m) : mult28(c1.m, ao.m); d = p / 64.0 - (double)m1 * a / 2048; } else { u64 a3 = (u64)a * a * a; i64 p = mult28(0xA56u << 2, (uint32_t)(a3 >> 4)); d = p / 64.0 - 0xA56 * (double)a * a * a / pow(2, 28); } if (d < mn) mn = d; if (d > mx) mx = d; sum += d; }
        printf("%s: array - exact (units): min %.5f mean %.5f max %.5f\n", cfg == 0 ? "linear a<<15 recoded" : cfg == 1 ? "linear a<<15 multiplicand" : "cubic C3<<2 x a^3>>4", mn, sum / 511, mx); }
    return 0; }
