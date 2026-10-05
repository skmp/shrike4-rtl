#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[513]; for (int a = 1; a <= 512; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; double c1m = k * pow(2, 14) * pow(2, 23);
    for (int amode = -1; amode <= 14; amode++) for (int rec = 0; rec < 2; rec++) { int nsurv = 0; int first = 999;
        for (int dC1 = -160; dC1 <= 160; dC1++) { Op c1 { (uint32_t)llround(c1m) + dC1, -14 }; bool ok = true; i64 t512 = 0;
            for (int a = 1; a <= 512 && ok; a++) { Op ao = amode < 0 ? op_norm(a) : op_fixed(a, amode); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); i64 r = T1 - V[a]; if (r < 0 || (a <= 24 && r > 1) || r > 1400) ok = false; t512 = r; }
            if (ok) { nsurv++; if (first == 999) first = dC1; if (nsurv <= 3) printf("  amode %2d rec %d dC1 %4d survives, T3req(512)=%lld\n", amode, rec, dC1, (long long)t512); } }
        printf("amode %2d rec %d: %d surviving dC1\n", amode, rec, nsurv); }
    return 0; }
