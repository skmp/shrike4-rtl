// v20_full.cpp -- independent FULL-MODEL re-run (nodes 0..31) with the square latch taken at the
// FINE scale (u2 -> u2+2) and produced by a parameterised chain, exactly as the w-kc-joint branch
// claims to have done.  Same refit policy as b2.cpp (C0 over its t=0 window +-3, u2 +-1).
#include "base2.h"
#include <cstring>
#include <cstdio>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} ex=w.exact; }
struct Cfg{ const char*name; int w,s1,s2; i64 kc,K1,K2; };
static i64 JT[1024];
static void fillJ(const Cfg&c){ for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<c.w;
    i64 I1=(arr(c.kc,dx)+c.K1)>>c.s1; JT[t+512]=(arr(I1,dx)+c.K2)>>c.s2; } }
static inline i64 Vof(int n,int t,int flip,i64 C0,int u2){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 T2=T.S? sm(arr(JT[t+512],T.S)+K,u2):0;
    i64 s=flip?-1:1; return C0+s*T1-T2-s*T3; }
static void run(const Cfg&c,bool verbose){
    fillJ(c);
    int grand=0, guard=0;
    printf("%-42s ",c.name);
    if(verbose) printf("\n  tab   C0        u2   viol(n)  viol(32-n)\n");
    for(int n=0;n<=16;n++){
        int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int bv=1<<30; i64 bc=lo0; int bu=TB[n].u2+2; int bper[2]={0,0};
        for(i64 cc=lo0-3;cc<=hi0+3;cc++) for(int u=TB[n].u2+1;u<=TB[n].u2+3;u++){
            int v=0,per[2]={0,0};
            for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17); int tn=flip?32-m:m;
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    i64 V=Vof(tn,t,flip,cc,u); if(V<lo||V>hi){v++;per[i]++;} } }
            if(v<bv){bv=v;bc=cc;bu=u;bper[0]=per[0];bper[1]=per[1];} }
        grand+=bv; if(n==0||n==1||n==2||n==16) guard+=bv;
        if(verbose) printf("  %2d  %9lld   %2d   %5d    %5d\n",n,(long long)bc,bu,bper[0],bper[1]);
    }
    printf("  TOTAL window violations = %4d   guard nodes(0,1,2,16) = %d\n",grand,guard);
}
int main(){ if(!load_fsca(F)){printf("no dump\n");return 1;} build_tables();
    puts("full model over nodes 0..31, square latch at the FINE scale (u2+2), C0/u2 refit as in b2.cpp\n");
    Cfg base{"baseline w=13 s1=23 s2=18 KC=0x4EF145",13,23,18,KC,K,K};
    run(base,false);
    run(Cfg{"claimed 925: w=14 s1=27 s2=19",14,27,19,41388583,K,K},false);
    run(Cfg{"claimed 925: w=14 s1=28 s2=19",14,28,19,82777166,K,K},false);
    run(Cfg{"claimed 937: w=12 s1=26 s2=17 freeK",12,26,17,82777165,5963776,65536},false);
    run(Cfg{"claimed 940: w=16 s1=30 s2=21 K2<0",16,30,21,82777165,85590016,-2293760},false);
    return 0; }
