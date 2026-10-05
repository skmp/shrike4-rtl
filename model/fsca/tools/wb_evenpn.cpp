// wb_evenpn.cpp -- PER-NODE test of "is the even part exactly even in t?".  For every node instance and
// every |t|, subtract the modelled odd part from the +t and -t windows and intersect.  If the intersection
// is non-empty everywhere, no sign asymmetry is needed in the square path at all -- the apparent asymmetry
// would be an artefact of forcing ONE shared latch J(t) across all nodes.
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    int totbad=0; puts("node  |t| with EMPTY even-part intersection   (worst gap)");
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        int bad=0; i64 worst=0; std::vector<int> badt;
        for(int A=1;A<=511;A++){
            i64 lo=-(1ll<<60),hi=(1ll<<60);
            for(int sg=0;sg<2;sg++){ int t=sg?-A:A; i64 dx=(i64)t<<13;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 O=s*T1-s*T3;
                int a=(node*1024+t)&0xffff; i64 l,h;bool ex; win_of(a,l,h,ex);
                if(l-O>lo)lo=l-O; if(h-O<hi)hi=h-O; }
            if(lo>hi){ bad++; if(lo-hi>worst)worst=lo-hi; if((int)badt.size()<14)badt.push_back(A); } }
        totbad+=bad;
        printf("%3d  %4d  (%lld)  ",node,bad,(long long)worst);
        for(int v:badt)printf("%d ",v); printf("\n"); }
    printf("TOTAL |t| with an empty even-part intersection over nodes 0..31: %d\n",totbad);
    return 0; }
