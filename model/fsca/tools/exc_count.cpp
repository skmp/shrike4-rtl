#include "../archive/fsca_ref.h"
#include "fsca_common.h"
int main() { Fsca F; if (!load_fsca(F)) return 1; int n1 = 0, n2 = 0, half = 0; for (uint32_t i = 0; i < 65536; i++) { i64 V = fsca_internal(i); Win w = decode(F.srz[i], F.srn[i]); i64 m = V < 0 ? -V : V; int sg = V < 0 ? -1 : 1; bool bad = (m < (i64)w.lo || m > (i64)w.hi || (m && sg != w.sign)); if (bad) { n1++; if (m < (i64)w.lo - 1 || m > (i64)w.hi + 1) n2++; if (i < 32768) half++; } }
    printf("angles where the model's internal value is outside the hardware window: %d of 65536 (%d in the first half turn; %d differ by more than one unit)\n", n1, half, n2); return 0; }
