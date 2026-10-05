// b33.cpp -- robustness of the model-free asymmetry proof (b27): re-run it under BOTH cubic truncation
// conventions that nodes 0/32 admit, and under both A3 latch forms, and with C0 free in its t=0 window
// (C0 cancels in the two-sided comparison, so it cannot matter -- shown explicitly).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static inline i64 tc(i64 P,int u,int c){ return c? (P>=0? (P>>u) : -((-P)>>u)) : (P>=0? (P>>u) : -((~P)>>u)); }
static inline i64 a3f(i64 P,int c){ return c? ((P+K)>>23) : (((P+K)>>20)>>3); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int c3c=0;c3c<2;c3c++) for(int a3c=0;a3c<2;a3c++){
        int tot=0; std::vector<int> ts;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
            for(int a=1;a<=511;a++){ i64 il=-(1ll<<60), ih=(1ll<<60);
                for(int sg=0;sg<2;sg++){ int t=sg?-a:a; i64 dx=(i64)t<<13;
                    i64 T1=TB[n].C1f? tc(arr(TB[n].C1f,dx)+K,23-TB[n].e1,0):0;
                    i64 A3=a3f(arr(((i64)t*t)<<3,dx),a3c); i64 T3=TB[n].c3? tc(arr(TB[n].c3<<5,A3)+K,26,c3c):0;
                    i64 O=s*T1-s*T3; int ang=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(ang,lo,hi,ex);
                    if(lo-O>il) il=lo-O; if(hi-O<ih) ih=hi-O; }
                if(il>ih){ tot++; if((int)ts.size()<60) ts.push_back(a); } } }
        // distinct |t|
        std::vector<int> d=ts; std::sort(d.begin(),d.end()); d.erase(std::unique(d.begin(),d.end()),d.end());
        printf("cubic conv=%d  A3 form=%d : %d (node,|t|) pairs prove the even part is asymmetric; distinct |t|:",c3c,a3c,tot);
        for(int v:d) printf(" %d",v); printf("\n"); }
    return 0; }
