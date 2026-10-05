#include "fsca_common.h"
#include <cmath>
#include <algorithm>
int main() { Fsca F; if (!load_fsca(F)) { printf("no dump\n"); return 1; }
    // node 0: right side i=a (1..512), left side i=65536-a. Exact internal values.
    i64 Vr[513], Vl[513]; int bad = 0;
    for (int a = 1; a <= 512; a++) { Win wr = decode(F.srz[a], F.srn[a]), wl = decode(F.srz[65536 - a], F.srn[65536 - a]); if (!wr.exact || !wl.exact || wr.sign != 1 || wl.sign != -1) bad++; Vr[a] = wr.lo; Vl[a] = wl.lo; }
    printf("node0: non-exact/sign issues %d; V(1)=%lld V(512)=%lld V(65535)=-%lld\n", bad, (long long)Vr[1], (long long)Vr[512], (long long)Vl[1]);
    // check odd symmetry sin(i+0x8000) = -sin(i) in RZ and RN
    int asym = 0; for (int i = 0; i < 32768; i++) { if ((F.srz[i] ^ F.srz[i + 32768]) != 0x80000000u && !(F.srz[i] == 0 && F.srz[i+32768] == 0)) asym++; if ((F.srn[i] ^ F.srn[i + 32768]) != 0x80000000u && !(F.srn[i] == 0 && F.srn[i+32768] == 0)) asym++; }
    int nexact = 0; for (int i = 0; i < 65536; i++) nexact += decode(F.srz[i], F.srn[i]).exact; printf("odd-symmetry violations (rz+rn) %d; exact entries %d of 65536\n", asym, nexact);
    // linear scan: C1 true = 2pi/65536 -> mantissa at exponent -14
    double k = 2 * M_PI / 65536; double c1m = k * pow(2, 14) * pow(2, 23); printf("C1 true mantissa %.3f (exp -14)\n", c1m);
    struct Res { int agree; int amode, rec, dC1; }; std::vector<Res> res;
    for (int amode = -1; amode <= 14; amode++) for (int rec = 0; rec < 2; rec++) {
        int best = -1, bestd = 0;
        for (int dC1 = -96; dC1 <= 96; dC1++) { Op c1 { (uint32_t)llround(c1m) + dC1, -14 }; int agree = 0;
            for (int a = 1; a <= 512; a++) { Op ao = amode < 0 ? op_norm(a) : op_fixed(a, amode); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1);
                i64 T3r = T1 - Vr[a], T3l = Vl[a] + T1;     // right: W = T1 - T3 ; left: W = -(T1 - T3) -> Vl = -T1 + T3
                if (T3r == T3l && T3r >= 0) agree++; }
            if (agree > best) { best = agree; bestd = dC1; } }
        res.push_back({ best, amode, rec, bestd }); }
    std::sort(res.begin(), res.end(), [](const Res& x, const Res& y) { return x.agree > y.agree; });
    printf("best (agree of 512, a-mode [-1=normalised, else fixed shift], recoded [0=coef,1=a], dC1):\n"); for (int i = 0; i < 10; i++) printf("  %d  amode %d rec %d dC1 %d\n", res[i].agree, res[i].amode, res[i].rec, res[i].dC1);
    // print implied T3 for the best
    Res b = res[0]; Op c1 { (uint32_t)llround(c1m) + b.dC1, -14 }; printf("implied T3 (best):"); for (int a : {1, 2, 3, 4, 8, 16, 32, 64, 128, 256, 384, 448, 512}) { Op ao = b.amode < 0 ? op_norm(a) : op_fixed(a, b.amode); i64 T1 = b.rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); printf(" a=%d:%lld/%lld", a, (long long)(T1 - Vr[a]), (long long)(Vl[a] + T1)); } printf("\n");
    return 0; }
