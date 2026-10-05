#include "fsca_common.h"
#include <cmath>
int main(int argc, char** argv) { int sp = atoi(argv[1]), rec = atoi(argv[2]); i64 K = argc > 3 ? atoll(argv[3]) : (1ll << 19); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 T1hw[512];
    g_K = 1ll << 19; for (int a = 1; a <= 511; a++) { i64 V = decode(F.srz[a], F.srn[a]).lo; u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 3), 26 }, c3 { 0xA56u << 1, -54 + 23 - 1 }; T1hw[a] = V + prod_units(c3, cu); }
    g_K = K; int mism = 0; for (int a = 1; a <= 511; a++) { Op c1 { (uint32_t)m1t, -14 }, ao = op_fixed(a, sp); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); if (T1 != T1hw[a]) { mism++; double e1 = (double)m1t * a / pow(2, 11);
        // digits of the recoded operand and the low-16 dropped bits per row, group terms
        uint32_t recv = rec == 0 ? c1.m : ao.m, mul = rec == 0 ? ao.m : c1.m; u64 R[13]; i64 drop = 0; for (int kk = 0; kk < 13; kk++) { int d = booth_digit(recv, kk); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[kk] = oc << (2*kk); drop += R[kk] & 0xffff; }
        int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1), p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1;
        printf("a=%3d hw %lld model %lld (%+lld) exact frac %.4f | drop %lld (%.4f u) maj15 %d p16 %d digits:", a, (long long)T1hw[a], (long long)T1, (long long)(T1 - T1hw[a]), e1 - floor(e1), (long long)drop, drop / pow(2, 23), m15 >= 2, p16); for (int kk = 0; kk < 13; kk++) printf(" %d", booth_digit(recv, kk)); printf("\n"); } }
    printf("mismatches %d\n", mism); return 0; }
