#include "fsca_common.h"
int main() { Fsca F; if (!load_fsca(F)) return 1; for (int i : {0, 1024, 2048, 3072, 30720, 31744, 32768, 33792, 34816, 63488, 64512}) { Win w = decode(F.srz[i], F.srn[i]); printf("i=%5d: V=%s%lld exact=%d (rz %08x rn %08x)\n", i, w.sign < 0 ? "-" : "", (long long)w.lo, w.exact, F.srz[i], F.srn[i]); } return 0; }
