#include "../archive/fsca_ref.h"
#include "fsca_common.h"
int main() { Fsca F; if (!load_fsca(F)) return 1; int bs[2] = {0,0}, bc[2] = {0,0}, big = 0; int shown = 0;
    for (uint32_t i = 0; i < 65536; i++) { uint32_t sn = fsca_sin(i, 0), sz = fsca_sin(i, 1), cn = fsca_cos(i, 0), cz = fsca_cos(i, 1); bs[0] += sn != F.srn[i]; bs[1] += sz != F.srz[i]; bc[0] += cn != F.crn[i]; bc[1] += cz != F.crz[i];
        if (sz != F.srz[i]) { Win w = decode(F.srz[i], F.srn[i]); i64 V = fsca_internal(i); i64 m = V < 0 ? -V : V; i64 d = m < (i64)w.lo ? m - (i64)w.lo : m - (i64)w.hi; if (d < -1 || d > 1) big++; if (shown < 3) { shown++; printf("  i=%u model V=%lld hw window [%lld,%lld]\n", i, (long long)V, (long long)w.lo, (long long)w.hi); } } }
    printf("fsca_ref vs dump: sin RN %d, sin RZ %d, cos RN %d, cos RZ %d mismatches of 65536 each; sin RZ mismatches beyond +-1 unit: %d\n", bs[0], bs[1], bc[0], bc[1], big); return 0; }
