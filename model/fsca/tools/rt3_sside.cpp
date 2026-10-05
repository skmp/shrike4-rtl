// rt3_sside.cpp -- DIAGNOSTIC FIT (not a model): does a sine field that differs between the two sides of the node
// (S_n^+ for t >= 0, S_n^- for t < 0; 34 stored values) absorb the residual that a single shared J cannot?
// The asymmetry witnesses at different |t| come from different nodes (342: nodes 3/13, 376: nodes 5/7), so a
// per-(node, side) field is a candidate "additional stored quantity".  Everything else is base2.h unchanged.
// Output: per node the best S^+ / S^- (offset from RN23), violations before/after, and the grand total.
#include "base2.h"
static Fsca F;
static inline void win_of(int a, i64& lo, i64& hi){ Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Pre { i64 odd; i64 J; i64 lo, hi; };   // per (node instance, t): odd part s*T1 - s*T3, the chain's J, window
int main(int argc, char** argv){ if(!load_fsca(F))return 1; build_tables();
    int R = argc > 1 ? atoi(argv[1]) : 64;   // scan radius on S
    int grand_before = 0, grand_after = 0;
    puts("tab   S(RN23)   best S+ (d)   best S- (d)   viol shared -> split   [n0 viol, n32-n viol]");
    for(int n=1;n<=16;n++){ const Tab&T=TB[n]; int nodes[2]={n,32-n}; int nn=(n==16)?1:2;
        std::vector<Pre> P[2];
        for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=m>=17; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20; int a=(m*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
                P[t<0].push_back({s*T1-s*T3, J, lo, hi}); } }
        auto viol=[&](int side, i64 S){ int v=0; for(auto&p:P[side]){ i64 T2=sm(arr(p.J,S)+K,T.u2); i64 V=C0TAB[n]+p.odd-T2; if(V<p.lo||V>p.hi)v++; } return v; };
        int v0p=viol(0,T.S), v0m=viol(1,T.S);
        int bp=v0p, bm=v0m; i64 Sp=T.S, Sm=T.S;
        for(i64 d=-R; d<=R; d++){ int vp=viol(0,T.S+d); if(vp<bp||(vp==bp&&llabs(d)<llabs(Sp-T.S))){bp=vp;Sp=T.S+d;} int vm=viol(1,T.S+d); if(vm<bm||(vm==bm&&llabs(d)<llabs(Sm-T.S))){bm=vm;Sm=T.S+d;} }
        grand_before += v0p+v0m; grand_after += bp+bm;
        printf("%2d  %8lld   %8lld (%+3lld)   %8lld (%+3lld)   %3d -> %3d   (+side %d->%d, -side %d->%d)\n",n,(long long)T.S,(long long)Sp,(long long)(Sp-T.S),(long long)Sm,(long long)(Sm-T.S),v0p+v0m,bp+bm,v0p,bp,v0m,bm);
    }
    printf("nodes 1..31 window violations: shared S %d -> split S+/S- %d  (scan +-%d)\n",grand_before,grand_after,R);
    return 0; }
