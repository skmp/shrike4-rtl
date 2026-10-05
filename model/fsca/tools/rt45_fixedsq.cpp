// rt45_fixedsq.cpp -- PORT OF THE FSRRA RESULT (fsrra/tools/rr10..rr14: FSRRA is bit-exact once the quadratic coefficient is
// a FIXED-POINT per-node field multiplying the ONE shared square latch (arr(dx,dx)+K)>>23, aligner shift fixed).
// FSCA's square latch M0>>3 = 8t^2 is the same 21-bit latch.  Direct port:
//     sq = ((arr(dx,dx) + K) >> 20) >> g
//     T2 = sm(arr(C2f_n << p, sq) + K, 20 + a)     or roles swapped;     C2f_n = per-node integer = sin * pi^2 * 2^F (economised), a = F + p - g - 9
// C2f_n is FITTED per node (DIAGNOSTIC: 16 free integers) -- the question is only whether ANY such single-product form
// reaches (near) zero on the full model, as it does for FSRRA.  Baseline (KC chain, derived tables): 148.
#include "base2.h"
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024];
struct Res { int viol, guard; int g,p,a,r; i64 c[17]; unsigned char pn[17]; };
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    static i64 SQ[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; SQ[t+512]=(arr(dx,dx)+K)>>20; }
    std::vector<Res> cfgs; for(int g=0;g<=6;g++)for(int p=0;p<=12;p++)for(int fb=8;fb<=20;fb++)for(int r=0;r<2;r++){ int a=fb+p-g-9; if(a<0||a>20)continue; Res c{}; c.g=g;c.p=p;c.a=a;c.r=r; cfgs.push_back(c); }
    printf("%zu configurations\n",cfgs.size());
    #pragma omp parallel for schedule(dynamic,2)
    for(size_t ci=0;ci<cfgs.size();ci++){ Res&c=cfgs[ci]; int fb=c.a-c.p+c.g+9; int v=0,gd=0; bool bad=false;
        for(int n=1;n<=16&&!bad;n++){ long double nom=sinl(3.14159265358979323846L*n/32)*powl(2.0L,fb)*(0x4ef145/262144.0L)/2; i64 c0=(i64)llroundl(nom); int best=1<<30; i64 bc=c0;
            if(((c0+50)<<c.p)>=(1ll<<(c.r?24:27))){bad=true;break;}
            for(i64 cc=c0-40;cc<=c0+40;cc++){ if(cc<=0)continue; i64 cf=cc<<c.p; int vv=0;
                for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; for(int t=0;t<1024;t++){ i64 sq=SQ[t]>>c.g; i64 P=c.r?arr(sq,cf):arr(cf,sq); i64 T2=sm(P+K,20+c.a); const Cell&cl=CELL[m][t]; i64 V=cl.base-T2; if(V<cl.lo||V>cl.hi){ if(++vv>=best)break; } } if(vv>=best)break; }
                if(vv<best){best=vv;bc=cc;} }
            c.c[n]=bc; c.pn[n]=best>255?255:best; v+=best; if(n<=2||n==16)gd+=best; }
        c.viol=bad?(1<<30):v; c.guard=gd; }
    std::sort(cfgs.begin(),cfgs.end(),[](const Res&x,const Res&y){ return x.viol<y.viol; });
    puts("best 25:  viol guard |  g  p  a  F | role (1 = square recoded) | per word 1..16");
    for(int i=0;i<25;i++){ const Res&c=cfgs[i]; if(c.viol>=(1<<30))break; printf("   %5d %5d | %2d %2d %2d %2d |   %d   | ",c.viol,c.guard,c.g,c.p,c.a,c.a-c.p+c.g+9,c.r); for(int n=1;n<=16;n++)printf("%d ",c.pn[n]); puts(""); }
    { const Res&c=cfgs[0]; printf("best: fitted C2f per word:"); for(int n=1;n<=16;n++)printf(" %lld",(long long)c.c[n]); puts(""); }
    return 0; }
