// we_a2.cpp -- second attribution round: per-t knobs that are proportional to cos (cubic / linear
// coefficient) rather than to sin (square latch).  If a cos-proportional knob also reaches ~2, the
// asymmetry could live in an odd term; if not, it is definitively in the sin-proportional path.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    struct Inst{int n,flip;i64 lo,hi;}; std::vector<Inst> ins[1024];
    for(int t=-512;t<=511;t++) for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex); ins[t+512].push_back({n,flip,lo,hi}); }
    auto Vof=[&](int n,int t,int flip,i64 dJ,i64 dc3,i64 dC1)->i64{ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
        i64 C1=T.C1f?T.C1f+dC1:0; i64 T1=C1? sm(arr(C1,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 c3=T.c3?T.c3+dc3:0; i64 T3=c3? sm(arr(c3<<5,A3)+K,26):0;
        i64 I1=(arr(KC,dx)+K)>>23, I2=((arr(I1,dx)+K)>>20)+dJ; i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
        i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3; };
    auto scan=[&](const char*name,int which,i64 rlo,i64 rhi){ int tot=0;
        #pragma omp parallel for reduction(+:tot) schedule(dynamic)
        for(int t=-512;t<=511;t++){ int bv=1<<30;
            for(i64 d=rlo;d<=rhi;d++){ int v=0;
                for(auto&I:ins[t+512]){ i64 V = which==0?Vof(I.n,t,I.flip,d,0,0): which==1?Vof(I.n,t,I.flip,0,d,0):Vof(I.n,t,I.flip,0,0,d);
                    if(V<I.lo||V>I.hi){ if(++v>=bv) break; } }
                if(v<bv)bv=v; }
            tot+=bv; }
        printf("%-44s min violations: %d\n",name,tot); };
    scan("(a) free square latch J        (prop. sin)",0,-60,60);
    scan("(f) free cubic coefficient c3  (prop. cos)",1,-400,400);
    scan("(g) free linear field C1       (prop. cos)",2,-400,400);
    return 0; }
