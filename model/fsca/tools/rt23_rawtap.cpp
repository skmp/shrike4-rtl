// rt23_rawtap.cpp -- the constant-first square chain with NON-STANDARD TAPS of the raw array output.
// rt22 shows the hardware's J is the chain's J minus a data-dependent 0..4 units (one-signed at small |t|), and that the
// chain's own first-latch floor seems ABSENT in the hardware (opposite-sign errors only where t/128 >= 1).  The array's
// column-16 truncation loses 0..2^19.3 ABSOLUTE; seen through a tap at raw bit 17 or 18 that is 0..5 or 0..2.5 J units.
// A tap below bit 20 requires dx to enter that multiply at a lower placement (the array output exists from column 16).
//   I1 = (arr(KC<<h, t<<a) + K1) >> (10 + a + h - f)      f = extra fraction bits kept in the first latch
//   J  = (arr(I1,     t<<b) + K2) >> (7 + b + f)          always the coarse scale; T2 = sm(arr(J,S)+K, 30+k) unchanged
// Taps must be >= 16.  K1,K2 in {0, 2^19}.  Roles free in both products.  Full model, guard nodes reported.
#include "base2.h"
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
struct Res { int viol, guard; int a,b,h,f,r,k1,k2; unsigned char pn[17]; };
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    std::vector<Res> cfgs;
    for(int a=6;a<=13;a++)for(int b=6;b<=13;b++)for(int h=0;h<=4;h++)for(int f=0;f<=6;f++){ int s1=10+a+h-f, s2=7+b+f; if(s1<16||s2<16)continue;
        for(int r=0;r<4;r++)for(int k1=0;k1<2;k1++)for(int k2=0;k2<2;k2++){ Res c{}; c.a=a;c.b=b;c.h=h;c.f=f;c.r=r;c.k1=k1;c.k2=k2; cfgs.push_back(c);} }
    printf("%zu configurations (model = a13 b13 h0 f0 roles 0 K 1 1)\n",cfgs.size());
    #pragma omp parallel for schedule(dynamic,8)
    for(size_t ci=0;ci<cfgs.size();ci++){ Res&c=cfgs[ci]; static thread_local i64 J[1024]; bool bad=false; int s1=10+c.a+c.h-c.f, s2=7+c.b+c.f; i64 KCh=KC<<c.h;
        for(int t=-512;t<=511;t++){ i64 da=(i64)t<<c.a, db=(i64)t<<c.b; i64 P1=(c.r&1)? arr(da,KCh):arr(KCh,da); i64 I1=(P1+(c.k1?K:0))>>s1; if(I1>=(1ll<<26)||I1<-(1ll<<26)){bad=true;break;}
            i64 P2=(c.r&2)? arr(db,I1):arr(I1,db); i64 j=(P2+(c.k2?K:0))>>s2; if(j<0||j>=(1ll<<27)){bad=true;break;} J[t+512]=j; }
        if(bad){c.viol=1<<30;continue;} int v=0,gd=0,cnt[17]={0};
        for(int m=1;m<32;m++){ const Tab&T=TB[TABN[m]]; bool g=(m<=2||m==16||m>=30); for(int i=0;i<1024;i++){ const Cell&cl=CELL[m][i]; i64 T2=sm(arr(J[i],T.S)+K,T.u2); i64 V=cl.base-T2; if(V<cl.lo||V>cl.hi){v++;cnt[TABN[m]]++; if(g)gd++;} } }
        c.viol=v;c.guard=gd; for(int n=0;n<17;n++)c.pn[n]=cnt[n]>255?255:cnt[n]; }
    std::sort(cfgs.begin(),cfgs.end(),[](const Res&x,const Res&y){ return x.viol<y.viol; });
    puts("best 25:  viol guard |  a  b  h  f roles K1 K2 | taps s1 s2 | per word 1..16");
    for(int i=0;i<25;i++){ const Res&c=cfgs[i]; printf("   %5d %5d | %2d %2d %2d %2d   %d    %d  %d |  %2d %2d   | ",c.viol,c.guard,c.a,c.b,c.h,c.f,c.r,c.k1,c.k2,10+c.a+c.h-c.f,7+c.b+c.f); for(int n=1;n<=16;n++)printf("%d ",c.pn[n]); puts(""); }
    return 0; }
