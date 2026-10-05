// rt20_orders.cpp -- all multiplication ORDERS of the square term {KC, S_n, dx, dx} as a left-to-right chain of three
// products through the array, every latch a standard product register (raw + K) >> 20 followed by a feedback shift
// g in 0..7, scored on the FULL model WITHOUT using "exact on nodes 1,2,16" as a filter (pat_lead4 kept only the
// guard-exact arrangements; rt17/rt19 show J itself is right and the defect is where S meets the chain, so the
// order question has to be re-asked with the total score).
//   total right shift must be 73 + k  ->  final aligner shift a = 13 + k - g1 - g2   (T2 = sm(P3 + K, 20 + a))
//   roles r1,r2,r3: 0 = the running value / first operand is RECODED (port B), 1 = it is the multiplicand (port A).
#include "base2.h"
#include <algorithm>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
struct Res { int viol, guard; int ord,g1,g2,r,dk; unsigned char pn[17]; };
static const char* ONAME[12]={"KC dx dx S","KC dx S dx","KC S dx dx","dx KC dx S","dx KC S dx","dx dx KC S","dx dx S KC","dx S KC dx","dx S dx KC","S KC dx dx","S dx KC dx","S dx dx KC"};
static const int ORD[12][4]={{0,2,2,1},{0,2,1,2},{0,1,2,2},{2,0,2,1},{2,0,1,2},{2,2,0,1},{2,2,1,0},{2,1,0,2},{2,1,2,0},{1,0,2,2},{1,2,0,2},{1,2,2,0}};   // 0 KC, 1 S, 2 dx
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    std::vector<Res> cfgs; for(int o=0;o<12;o++)for(int g1=0;g1<=7;g1++)for(int g2=0;g2<=7;g2++)for(int r=0;r<8;r++)for(int dk=-2;dk<=2;dk++){ Res c{}; c.ord=o;c.g1=g1;c.g2=g2;c.r=r;c.dk=dk; cfgs.push_back(c); }
    printf("%zu configurations\n",cfgs.size());
    #pragma omp parallel for schedule(dynamic,8)
    for(size_t ci=0;ci<cfgs.size();ci++){ Res&c=cfgs[ci]; int cnt[17]={0}; int v=0,gd=0; bool bad=false;
        for(int n=1;n<=16&&!bad;n++){ const Tab&T=TB[n]; int a=13+(T.u2-30)-c.g1-c.g2; if(a<0||a>40){bad=true;break;}
            for(int t=-512;t<=511&&!bad;t++){ i64 dx=(i64)t<<13; i64 op[3]={KC+c.dk,T.S,dx}; const int* O=ORD[c.ord];
                auto mulp=[&](i64 run,i64 nxt,int role)->i64{ i64 rec=role?nxt:run, mul=role?run:nxt; if(rec>=(1ll<<27)||rec<-(1ll<<27)||mul>=(1ll<<25)||mul<-(1ll<<25)){bad=true;return 0;} return arr(rec,mul); };
                i64 P1=mulp(op[O[0]],op[O[1]],c.r&1); if(bad)break; i64 L1=((P1+K)>>20)>>c.g1;
                i64 P2=mulp(L1,op[O[2]],(c.r>>1)&1); if(bad)break; i64 L2=((P2+K)>>20)>>c.g2;
                i64 P3=mulp(L2,op[O[3]],(c.r>>2)&1); if(bad)break; i64 T2=sm(P3+K,20+a);
                for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; const Cell&cl=CELL[m][t+512]; i64 V=cl.base-T2; if(V<cl.lo||V>cl.hi){ v++; cnt[n]++; if(n<=2||n==16)gd++; } } } }
        if(bad){c.viol=1<<30;continue;} c.viol=v; c.guard=gd; for(int n=0;n<17;n++)c.pn[n]=cnt[n]>255?255:cnt[n]; }
    std::sort(cfgs.begin(),cfgs.end(),[](const Res&x,const Res&y){ return x.viol<y.viol; });
    puts("best 30 overall:  viol guard | order        g1 g2 roles dKC | per word 1..16");
    for(int i=0;i<30;i++){ const Res&c=cfgs[i]; if(c.viol>=(1<<30))break; printf("   %5d %5d | %-11s  %d  %d   %d%d%d  %+d | ",c.viol,c.guard,ONAME[c.ord],c.g1,c.g2,c.r&1,(c.r>>1)&1,(c.r>>2)&1,c.dk); for(int n=1;n<=16;n++)printf("%d ",c.pn[n]); puts(""); }
    puts("best per order:");
    for(int o=0;o<12;o++){ for(auto&c:cfgs) if(c.ord==o&&c.viol<(1<<30)){ printf("   %-11s : %5d (guard %d)  g %d %d roles %d%d%d dKC %+d | ",ONAME[o],c.viol,c.guard,c.g1,c.g2,c.r&1,(c.r>>1)&1,(c.r>>2)&1,c.dk); for(int n=1;n<=16;n++)printf("%d ",c.pn[n]); puts(""); break; } }
    return 0; }
