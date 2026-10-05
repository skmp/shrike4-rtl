#include "fsca_common.h"
int main() { Fsca F; if (!load_fsca(F)) return 1; int idx[] = { 1231, 817, 1503, 545, 2256, 1840, 2325, 1771, 2356, 1740, 1230, 1232 };
    for (int i : idx) { Win w = decode(F.srz[i], F.srn[i]); printf("i=%5d rz=%08x rn=%08x %s V=%s%lld  cos: rz=%08x rn=%08x\n", i, F.srz[i], F.srn[i], F.srz[i] == F.srn[i] ? "same" : "DIFF", w.sign < 0 ? "-" : "", (long long)w.lo, F.crz[(i + 65536 - 16384) % 65536], F.crn[(i + 65536 - 16384) % 65536]); }
    int ndiff = 0; for (int i = 0; i < 65536; i++) { Win w = decode(F.srz[i], F.srn[i]); if (w.exact && F.srz[i] != F.srn[i]) ndiff++; } printf("exact-region entries where RZ != RN: %d\n", ndiff);
    return 0; }
