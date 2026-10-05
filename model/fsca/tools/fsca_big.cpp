#include "../archive/fsca_ref.h"
#include "fsca_common.h"
int main() { Fsca F; if (!load_fsca(F)) return 1; for (uint32_t i = 0; i < 65536; i++) { Win w = decode(F.srz[i], F.srn[i]); i64 V = fsca_internal(i); i64 m = V < 0 ? -V : V; int sg = V < 0 ? -1 : 1; if (m < (i64)w.lo - 1 || m > (i64)w.hi + 1 || (m && sg != w.sign)) { int n = (i + 512) >> 10; int t = (int)i - 1024 * n; printf("i=%u node %d t %d: model %lld, hw sign %d window [%lld,%lld]\n", i, n, t, (long long)V, w.sign, (long long)w.lo, (long long)w.hi); } } return 0; }
