// rt34_sqraw.cpp -- PATENT ORDER with a RAW-TAPPED, LOW-PLACEMENT SQUARE.
// rt33: at small |t| the hardware's J is forced up to 4.4 units BELOW floor(KC t^2/2^17) (t = 59) where every chain
// truncation is < 1 unit, i.e. t^2 itself is short by up to ~1/8.  arr(t<<a, t<<b) loses up to 2^19.3 ABSOLUTE; with
// a+b ~ 22 and a tap below the rounded register that is 0..0.155 t^2 = 0..6 J units, hash-like in t and different for
// -t (Booth digits of a negative operand).  rt5 forced the >>20 register and so never saw this.
//   Q = (arr(t<<a, t<<b) + K1) >> s1            s1 >= 16  (raw tap),  Q = t^2 * 2^(a+b-s1)
//   J = (arr(KC<<h, Q) + K2) >> s2              s2 = h + a + b - s1 + 17 >= 16     (either role)
//   T2 = sm(arr(J, S) + K, 30 + k)              unchanged
#include "base2.h"
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
struct Res { int viol, guard, small; int a,b,s1,h,r,k1,k2; unsigned char pn[17]; };
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    std::vector<Res> cfgs;
    for(int a=4;a<=13;a++)for(int b=4;b<=13;b++)for(int s1=16;s1<=26;s1++)for(int h=0;h<=4;h++){ int s2=h+a+b-s1+17; if(s2<16||s2>40)continue; if(18+a+b-s1>=27)continue;
        for(int r=0;r<2;r++)for(int k1=0;k1<2;k1++)for(int k2=0;k2<2;k2++){ Res c{}; c.a=a;c.b=b;c.s1=s1;c.h=h;c.r=r;c.k1=k1;c.k2=k2; cfgs.push_back(c);} }
    printf("%zu configurations\n",cfgs.size());
    #pragma omp parallel for schedule(dynamic,16)
    for(size_t ci=0;ci<cfgs.size();ci++){ Res&c=cfgs[ci]; i64 J[1024]; bool bad=false; int s2=c.h+c.a+c.b-c.s1+17; i64 KCh=KC<<c.h;
        for(int t=-512;t<=511;t++){ i64 Q=(arr((i64)t<<c.a,(i64)t<<c.b)+(c.k1?K:0))>>c.s1; if(Q<0)Q=0; if((c.r==0&&Q>=(1ll<<25))||(c.r==1&&Q>=(1ll<<27))){bad=true;break;}
            i64 P=c.r? arr(Q,KCh):arr(KCh,Q); i64 j=(P+(c.k2?K:0))>>s2; if(j<0||j>=(1ll<<27)){bad=true;break;} J[t+512]=j; }
        if(bad){c.viol=1<<30;continue;} int v=0,gd=0,sm_=0,cnt[17]={0};
        for(int m=1;m<32;m++){ const Tab&T=TB[TABN[m]]; bool g=(m<=2||m==16||m>=30); for(int i=0;i<1024;i++){ const Cell&cl=CELL[m][i]; i64 T2=sm(arr(J[i],T.S)+K,T.u2); i64 V=cl.base-T2; if(V<cl.lo||V>cl.hi){v++;cnt[TABN[m]]++; if(g)gd++; if(abs(i-512)<=127)sm_++;} } }
        c.viol=v;c.guard=gd;c.small=sm_; for(int n=0;n<17;n++)c.pn[n]=cnt[n]>255?255:cnt[n]; }
    std::sort(cfgs.begin(),cfgs.end(),[](const Res&x,const Res&y){ return x.viol<y.viol; });
    puts("(the chain: 148 total, 0 guard, 33 at |t|<=127)\nbest 20 by total:  viol guard small(|t|<=127) |  a  b s1  h s2 role K1 K2 | per word 1..16");
    for(int i=0;i<20;i++){ const Res&c=cfgs[i]; printf("   %5d %5d %5d            | %2d %2d %2d %2d %2d   %d   %d  %d | ",c.viol,c.guard,c.small,c.a,c.b,c.s1,c.h,c.h+c.a+c.b-c.s1+17,c.r,c.k1,c.k2); for(int n=1;n<=16;n++)printf("%d ",c.pn[n]); puts(""); }
    std::sort(cfgs.begin(),cfgs.end(),[](const Res&x,const Res&y){ return x.small!=y.small? x.small<y.small : x.viol<y.viol; });
    puts("best 10 on |t|<=127:");
    for(int i=0;i<10;i++){ const Res&c=cfgs[i]; printf("   %5d %5d %5d            | %2d %2d %2d %2d %2d   %d   %d  %d\n",c.viol,c.guard,c.small,c.a,c.b,c.s1,c.h,c.h+c.a+c.b-c.s1+17,c.r,c.k1,c.k2); }
    return 0; }
