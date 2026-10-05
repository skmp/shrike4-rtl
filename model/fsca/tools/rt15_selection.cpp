// rt6_chainplace.cpp -- companion of rt5_sqfirst.cpp: the MODEL-ORDER chain (constant x dx first) with the dx PLACEMENT
// varied independently in each multiply (the notes fixed t<<13 in both and varied only latch shifts).
//   M1 = reg(arr(KC<<h, t<<a)) ; I1 = M1 >> g1 ; M2 = reg(arr(I1, t<<b)) ; J = M2 >> g2 ; e = h+a+b-23-g1-g2
// (original header of rt5 follows)
// rt5_sqfirst.cpp -- RTL-disciplined scan of the PATENT-ORDER square path (FIG. 28: dx*dx first, then the global
// constant, then the table sine), scored on the FULL model (nodes 0..31 + guard nodes), not on jwin18.
//
// Motivation (rtl/fpu_vec): the array's truncation loss is ABSOLUTE (~2^17..2^19.3).  With both squarer operands
// at t<<13 it is 0.1 coarse J units and the register rounds it away (Q = 64 t^2 exactly, symmetric) -- which is
// why "dx^2 first" looked dead.  If either squarer port sees t at a LOWER placement (the patent's expander is
// "8-bit shift + 6-bit sign expansion"), the same loss is 3..12 coarse J units: deficit-biased, hash-like in t,
// sign-dependent through the Booth digits of a negative operand, firing at a minority of offsets.
//
// Discipline: every latch is a standard product register reg(P) = (P + K) >> 20, optionally shifted right by a
// feedback amount before re-entering a port (that is all the RTL needs for the published models).
//   Q  = reg(arr(t<<a, t<<b))                              a,b in 6..13   (rec, mul)
//   X  = Q >> g1
//   Jr = reg(arr(KC<<h, X))  or  reg(arr(X, KC<<h))        h in 0..4
//   J  = Jr >> g2 ;  scale e = h + a + b - 23 - g1 - g2    (J = KC t^2 / 2^17 * 2^e)
//   T2 = sm(arr(J, S) + K3, 30 + k + e)  or  arr(S, J)     K3 = 2^19 (pinned by the guard nodes)
// K1, K2 in {0, 2^19}.  Odd part, C0, S, u2 from base2.h unchanged.
#include "base2.h"
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base; i64 lo, hi; };            // base = C0 + odd part ; window
static Cell CELL[32][1024]; static int TABN[32];
struct Res { int viol, guard; int a,b,g1,h,g2,r2,r3,k1,k2,e; unsigned char pn[17]; };
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;}
            CELL[m][t+512]={C0TAB[n]+s*T1-s*T3, lo, hi}; } }
    auto score=[&](const i64* J,int e,int r3,int& guard,unsigned char* pn){ int v=0; guard=0; int cnt[17]={0};
        for(int m=0;m<32;m++){ const Tab&T=TB[TABN[m]]; if(!T.S){ for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; if(c.base<c.lo||c.base>c.hi){v++;guard++;cnt[TABN[m]]++;} } continue; }
            int u=T.u2+e; bool g=(m<=2||m==16||m>=30);
            for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; i64 P=r3? arr(T.S,J[i]) : arr(J[i],T.S); i64 T2=sm(P+K,u); i64 V=c.base-T2; if(V<c.lo||V>c.hi){ v++; cnt[TABN[m]]++; if(g)guard++; } } }
        if(pn) for(int n=0;n<17;n++) pn[n]=cnt[n]>255?255:cnt[n];
        return v; };
    // baseline for sanity
    { i64 J[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J[t+512]=(arr(I1,dx)+K)>>20; } int g; unsigned char pn0[17]; int v=score(J,0,0,g,pn0); printf("baseline per table entry:"); for(int n=0;n<17;n++)printf(" %d",pn0[n]); puts(""); printf("baseline chain: %d violations, %d on guard nodes\n",v,g); }
    std::vector<Res> cfgs;
    for(int a=6;a<=13;a++)for(int b=6;b<=13;b++)for(int g1=0;g1<=7;g1++)for(int h=0;h<=4;h++)for(int g2=0;g2<=7;g2++){
        int e=h+a+b-23-g1-g2; if(e<-3||e>3)continue; if(22+h+10+a-20-g1>=27)continue;      // |I1| < 2^26 or so
        for(int r2=0;r2<4;r2++)for(int r3=0;r3<2;r3++)for(int k1=0;k1<2;k1++)for(int k2=0;k2<2;k2++) cfgs.push_back({0,0,a,b,g1,h,g2,r2,r3,k1,k2,e}); }
    printf("%zu configurations\n",cfgs.size());
    #pragma omp parallel for schedule(dynamic,16)
    for(size_t ci=0;ci<cfgs.size();ci++){ Res&c=cfgs[ci]; i64 J[1024]; i64 K1=c.k1?K:0, K2=c.k2?K:0; i64 KCh=KC<<c.h; bool ok=true;
        for(int t=-512;t<=511&&ok;t++){ i64 da=(i64)t<<c.a, db=(i64)t<<c.b; i64 M1=(((c.r2&1)? arr(da,KCh):arr(KCh,da))+K1)>>20; i64 I1=M1>>c.g1; if(I1>=(1ll<<26)||I1<-(1ll<<26)){ok=false;break;}
            i64 M2=(((c.r2&2)? arr(db,I1):arr(I1,db))+K2)>>20; i64 j=M2>>c.g2; if(j<0||j>=(1ll<<27))ok=false; J[t+512]=j; }
        if(!ok){c.viol=1<<30;continue;} c.viol=score(J,c.e,c.r3,c.guard,c.pn); }
    // SELECTION TEST: for every triple-like "guard set" G of table entries, how clean can the family be made ON G,
    // and what does that structure then do elsewhere?  The published model was selected on G = {1,2,16}.
    auto report=[&](const char* nm,std::vector<int> G){ int best=1<<30; size_t bi=0; for(size_t i=0;i<cfgs.size();i++){ if(cfgs[i].viol>=(1<<30))continue; int g=0; for(int n:G)g+=cfgs[i].pn[n]; if(g<best||(g==best&&cfgs[i].viol<cfgs[bi].viol)){best=g;bi=i;} }
        const Res&c=cfgs[bi]; int n0=0; for(size_t i=0;i<cfgs.size();i++){ if(cfgs[i].viol>=(1<<30))continue; int g=0; for(int n:G)g+=cfgs[i].pn[n]; if(g==0)n0++; }
        printf("G=%-12s best-on-G %2d (%5d structures reach 0) | total %3d | per entry:",nm,best,n0,c.viol); for(int n=0;n<17;n++)printf(" %d",c.pn[n]); printf("   [a %d b %d g1 %d h %d g2 %d e %d r %d%d K %d%d]\n",c.a,c.b,c.g1,c.h,c.g2,c.e,c.r2,c.r3,c.k1,c.k2); };
    report("{1,2,16}",{1,2,16}); report("{3,4,5}",{3,4,5}); report("{6,7,8}",{6,7,8}); report("{9,10,11}",{9,10,11}); report("{12,13,14}",{12,13,14}); report("{13,14,15}",{13,14,15}); report("{5,9,13}",{5,9,13}); report("{4,8,12}",{4,8,12}); report("{9}",{9}); report("{13}",{13}); report("{15,16}",{15,16});
    return 0; }
