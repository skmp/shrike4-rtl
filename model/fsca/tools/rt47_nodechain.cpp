// rt47_nodechain.cpp -- second port of the exact FSRRA structure: FSRRA's cubic is a COEFFICIENT-FIRST chain through one
// latch, I = (arr(C3f<<8, sq) + K) >> 23 ; T3 = sm(arr(dx, I) + K, 27), with a per-node fixed-point field and NO global
// constant.  FSCA analogue for the even term (no KC, no common J; asymmetric in t, as the data demand):
//     I_n = (arr(C2f_n << p, dx) | swapped  + K) >> (20 + g1)        C2f_n ~ sin * pi^2 * 2^F  (FITTED per node: DIAGNOSTIC)
//     T2  = sm(arr(dx, I_n) | swapped  + K, 20 + a)                  a = F + p - g1 - 9
// CURRENT.md 6 lists "C2_n x dx x dx (149)" without its placements; this rescans it with the FSRRA-style placements.
#include "base2.h"
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024];
struct Res { int viol, guard; int fb,p,g1,a,r1,r2; i64 c[17]; unsigned char pn[17]; };
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    std::vector<Res> cfgs; for(int fb=10;fb<=22;fb++)for(int p=0;p<=10;p++)for(int g1=0;g1<=9;g1++){ int a=fb+p-g1-9; int ires=fb+p-7-g1; if(a<0||a>24||ires<4||ires>13)continue; for(int r=0;r<4;r++){ Res c{}; c.fb=fb;c.p=p;c.g1=g1;c.a=a;c.r1=r&1;c.r2=r>>1; cfgs.push_back(c); } }
    printf("%zu configurations\n",cfgs.size());
    #pragma omp parallel for schedule(dynamic,2)
    for(size_t ci=0;ci<cfgs.size();ci++){ Res&c=cfgs[ci]; int v=0,gd=0; bool bad=false;
        for(int n=1;n<=16&&!bad;n++){ long double nom=sinl(3.14159265358979323846L*n/32)*powl(2.0L,c.fb)*(0x4ef145/262144.0L)/2; i64 c0=(i64)llroundl(nom); int best=1<<30; i64 bc=c0; i64 lim=c.r1?(1ll<<24):(1ll<<27); if(((c0+4096)<<c.p)>=lim){bad=true;break;}
            auto eval=[&](i64 cc,int cap){ i64 cf=cc<<c.p; int vv=0; for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I=((c.r1?arr(dx,cf):arr(cf,dx))+K)>>(20+c.g1); i64 P=c.r2?arr(I,dx):arr(dx,I); i64 T2=sm(P+K,20+c.a); const Cell&cl=CELL[m][t+512]; i64 V=cl.base-T2; if(V<cl.lo||V>cl.hi){ if(++vv>=cap)return vv; } } } return vv; };
            i64 st=c.fb>16?(1ll<<(c.fb-16)):1; for(i64 cc=c0-40*st;cc<=c0+40*st;cc+=st){ int vv=eval(cc,best); if(vv<best){best=vv;bc=cc;} } if(st>1){ i64 mid=bc; for(i64 cc=mid-st;cc<=mid+st;cc++){ int vv=eval(cc,best); if(vv<best){best=vv;bc=cc;} } }
            c.c[n]=bc; c.pn[n]=best>255?255:best; v+=best; if(n<=2||n==16)gd+=best; }
        c.viol=bad?(1<<30):v; c.guard=gd; }
    std::sort(cfgs.begin(),cfgs.end(),[](const Res&x,const Res&y){ return x.viol!=y.viol?x.viol<y.viol:x.guard<y.guard; });
    puts("best 25:  viol guard |  F  p g1  a | coefficient is multiplicand / latch recoded | per word 1..16");
    for(int i=0;i<25;i++){ const Res&c=cfgs[i]; if(c.viol>=(1<<30))break; printf("   %5d %5d | %2d %2d %2d %2d |   %d %d   | ",c.viol,c.guard,c.fb,c.p,c.g1,c.a,c.r1,c.r2); for(int n=1;n<=16;n++)printf("%d ",c.pn[n]); puts(""); }
    for(auto&c:cfgs) if(c.guard==0&&c.viol<(1<<30)){ printf("best GUARD-CLEAN: %d  | F %d p %d g1 %d a %d roles %d %d\n",c.viol,c.fb,c.p,c.g1,c.a,c.r1,c.r2); break; }
    return 0; }
