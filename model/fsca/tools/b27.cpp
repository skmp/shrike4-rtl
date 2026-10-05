// b27.cpp -- MODEL-FREE test of the asymmetry.  Assume only that the odd part is s*(T1 - T3) as established
// (uniquely pinned by nodes 0 and 32) and that the even part is SOME function E(node, |t|) -- symmetric in t,
// whatever the datapath.  Then for each (node, |t|) the two hardware windows give two intervals for E; if they
// are disjoint, the even part provably cannot be symmetric.  No assumption about the square's structure at all.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    int tot=0; std::vector<std::pair<int,int>> bad;   // (node,|t|)
    int pernode[32]; memset(pernode,0,sizeof pernode);
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int a=1;a<=511;a++){ i64 il=-(1ll<<60), ih=(1ll<<60);
            for(int sgn=0;sgn<2;sgn++){ int t=sgn?-a:a; i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 O=s*T1-s*T3; int ang=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(ang,lo,hi,ex);
                // V = O + E'  where E' = C0 - T2 (the whole even part); E' must be in [lo-O, hi-O]
                il=std::max(il,lo-O); ih=std::min(ih,hi-O); }
            if(il>ih){ tot++; pernode[node]++; if((int)bad.size()<40) bad.push_back({node,a}); } } }
    printf("(node,|t|) pairs where the even part CANNOT be symmetric in t: %d of %d\n",tot,32*511);
    printf("per node:"); for(int i=0;i<32;i++) if(pernode[i]) printf(" %d:%d",i,pernode[i]); printf("\n");
    printf("first cases:"); for(auto&p:bad) printf(" (n%d,|t|=%d)",p.first,p.second); printf("\n");
    return 0; }
