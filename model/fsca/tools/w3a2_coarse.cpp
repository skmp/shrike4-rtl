#include "w3_common.h"
int main(){ if(!load_fsca(FD)) return 1; build_tables();
    DU=0; auto J20=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
    int g; int v=full_violations(J20,&g);
    printf("COARSE full-model (u2=30+k, latch>>20): %d violations (expect 147), guard %d (expect 0)\n", v, g);
    return 0; }
