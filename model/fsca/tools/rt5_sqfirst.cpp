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
struct Res { int viol, guard; int a,b,g1,h,g2,r2,r3,k1,k2,e; };
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;}
            CELL[m][t+512]={C0TAB[n]+s*T1-s*T3, lo, hi}; } }
    auto score=[&](const i64* J,int e,int r3,int& guard){ int v=0; guard=0;
        for(int m=0;m<32;m++){ const Tab&T=TB[TABN[m]]; if(!T.S){ for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; if(c.base<c.lo||c.base>c.hi){v++;guard++;} } continue; }
            int u=T.u2+e; bool g=(m<=2||m==16||m>=30);
            for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; i64 P=r3? arr(T.S,J[i]) : arr(J[i],T.S); i64 T2=sm(P+K,u); i64 V=c.base-T2; if(V<c.lo||V>c.hi){ v++; if(g)guard++; } } }
        return v; };
    // baseline for sanity
    { i64 J[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J[t+512]=(arr(I1,dx)+K)>>20; } int g; int v=score(J,0,0,g); printf("baseline chain: %d violations, %d on guard nodes\n",v,g); }
    std::vector<Res> cfgs;
    for(int a=6;a<=13;a++)for(int b=6;b<=13;b++)for(int g1=0;g1<=7;g1++)for(int h=0;h<=4;h++)for(int g2=0;g2<=7;g2++){
        int e=h+a+b-23-g1-g2; if(e<-1||e>3)continue; if(18+a+b-20-g1>=24)continue;      // X must fit the 25-bit signed port
        for(int r2=0;r2<2;r2++)for(int r3=0;r3<2;r3++)for(int k1=0;k1<2;k1++)for(int k2=0;k2<2;k2++) cfgs.push_back({0,0,a,b,g1,h,g2,r2,r3,k1,k2,e}); }
    printf("%zu configurations\n",cfgs.size());
    #pragma omp parallel for schedule(dynamic,16)
    for(size_t ci=0;ci<cfgs.size();ci++){ Res&c=cfgs[ci]; i64 J[1024]; i64 K1=c.k1?K:0, K2=c.k2?K:0; i64 KCh=KC<<c.h; bool ok=true;
        for(int t=-512;t<=511&&ok;t++){ i64 Q=(arr((i64)t<<c.a,(i64)t<<c.b)+K1)>>20; i64 X=Q>>c.g1; i64 Jr=((c.r2? arr(X,KCh):arr(KCh,X))+K2)>>20; i64 j=Jr>>c.g2; if(j<0||j>=(1ll<<27))ok=false; J[t+512]=j; }
        if(!ok){c.viol=1<<30;continue;} c.viol=score(J,c.e,c.r3,c.guard); }
    std::sort(cfgs.begin(),cfgs.end(),[](const Res&x,const Res&y){ return x.viol!=y.viol? x.viol<y.viol : x.guard<y.guard; });
    puts("best 25:  viol guard |  a  b g1  h g2  e | roles(step2 X-recoded, step3 S-recoded) K1 K2");
    for(int i=0;i<25&&i<(int)cfgs.size();i++){ const Res&c=cfgs[i]; printf("   %5d %5d | %2d %2d %2d %2d %2d %2d |   %d %d   %d %d\n",c.viol,c.guard,c.a,c.b,c.g1,c.h,c.g2,c.e,c.r2,c.r3,c.k1,c.k2); }
    // best guard-clean
    for(auto&c:cfgs) if(c.guard==0&&c.viol<(1<<30)){ printf("best GUARD-CLEAN: %d violations | a %d b %d g1 %d h %d g2 %d e %d roles %d %d K1 %d K2 %d\n",c.viol,c.a,c.b,c.g1,c.h,c.g2,c.e,c.r2,c.r3,c.k1,c.k2); break; }
    return 0; }
