#include "w3_common.h"
int main(){ if(!load_fsca(FD))return 1; build_tables(); if(!load_jwin())return 2;
    // best complement-family config from w3e: dx+1 for t<0, FIRST multiply only
    auto Jf=[&](int t){ i64 dxa=((i64)t<<13)+(t<0?1:0), dxb=(i64)t<<13;
        i64 I1=(arr(KC,dxa)+K)>>23; return (arr(I1,dxb)+K)>>18; };
    int g; DU=2; printf("FINE : jwin18 %d/1024  full %d (guard %d)\n", score_jwin(Jf,JLf,JHf), full_violations(Jf,&g), g);
    auto J20=[&](int t){ i64 dxa=((i64)t<<13)+(t<0?1:0), dxb=(i64)t<<13;
        i64 I1=(arr(KC,dxa)+K)>>23; return (arr(I1,dxb)+K)>>20; };
    int g2; DU=0; printf("COARSE: full %d (guard %d)   [baseline 148, guard 0]\n", full_violations(J20,&g2), g2);
    printf("pins: J(17)=%lld J(342)=%lld J(-342)=%lld\n",(long long)Jf(17),(long long)Jf(342),(long long)Jf(-342));
    return 0; }
