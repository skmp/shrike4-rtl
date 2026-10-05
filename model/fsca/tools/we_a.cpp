// we_a.cpp -- attribution: which single per-t knob can explain the residual?
//  (a) free J (square latch)        (b) free common additive delta on V
//  (c) free offset on A3 (cubic latch)  (d) free offset on the cubic PRODUCT  (e) free offset on the linear PRODUCT
// Each knob is one free integer per t shared by all 31 node instances.  Reports minimum window violations.
#include "we_win.h"
#include <cstring>
#include <algorithm>
#include <set>
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    struct Inst{int n,flip;i64 lo,hi;};        // V window
    std::vector<Inst> ins[1024];
    for(int t=-512;t<=511;t++) for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex); ins[t+512].push_back({n,flip,lo,hi}); }
    auto Vof=[&](int n,int t,int flip,i64 dJ,i64 dA3,i64 dP3,i64 dP1,i64 dV)->i64{ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K+dP1,23-T.e1):0;
        i64 A3=(((arr(((i64)t*t)<<3,dx)+K)>>20)>>3)+dA3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K+dP3,26):0;
        i64 I1=(arr(KC,dx)+K)>>23, I2=((arr(I1,dx)+K)>>20)+dJ; i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
        i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3+dV; };
    auto scan=[&](const char*name,int which,i64 rlo,i64 rhi){ int tot=0;
        #pragma omp parallel for reduction(+:tot) schedule(dynamic)
        for(int t=-512;t<=511;t++){ int bv=1<<30;
            for(i64 d=rlo;d<=rhi;d++){ int v=0;
                for(auto&I:ins[t+512]){ i64 V;
                    switch(which){case 0:V=Vof(I.n,t,I.flip,d,0,0,0,0);break; case 1:V=Vof(I.n,t,I.flip,0,0,0,0,d);break;
                        case 2:V=Vof(I.n,t,I.flip,0,d,0,0,0);break; case 3:V=Vof(I.n,t,I.flip,0,0,d,0,0);break;
                        default:V=Vof(I.n,t,I.flip,0,0,0,d,0);} 
                    if(V<I.lo||V>I.hi)v++; if(v>=bv)break; }
                if(v<bv)bv=v; }
            tot+=bv; }
        printf("%-34s min violations over nodes 0..31: %d\n",name,tot); };
    // baseline
    { int tot=0; for(int t=-512;t<=511;t++) for(auto&I:ins[t+512]){ i64 V=Vof(I.n,t,I.flip,0,0,0,0,0); if(V<I.lo||V>I.hi)tot++; }
      printf("%-34s %d\n","baseline (no knob):",tot); }
    scan("(a) free J (square latch)",0,-60,60);
    scan("(b) free common delta on V",1,-3,3);
    scan("(c) free offset on A3",2,-4000,4000);

    return 0; }
