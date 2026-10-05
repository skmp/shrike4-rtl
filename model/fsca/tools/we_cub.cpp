// we_cub.cpp -- could the REQUIRED sign asymmetry of the square latch be an artifact of the cubic latch?
// The D constraints are sub-LSB boundary calls; a small change in A3 (the t^3 latch) moves the J windows.
// Allow a free per-t offset d on A3 (range given) and ask whether the +/- J windows can then be made to
// OVERLAP, i.e. whether a symmetric square latch becomes possible.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static void winA3(int t,i64 dA3,i64&gl,i64&gh){ gl=0;gh=12000000; i64 dx=(i64)t<<13;
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
        const Tab&T=TB[n]; i64 s=flip?-1:1; int u=U2TAB[n];
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=(((arr(((i64)t*t)<<3,dx)+K)>>20)>>3)+dA3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        i64 t2lo=base-hi,t2hi=base-lo;
        i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
        if(a1>gl)gl=a1; if(a2<gh)gh=a2; } }
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int R=argc>1?atoi(argv[1]):24; int odd=argc>2?atoi(argv[2]):1;   // odd=1: d(-t) = -d(t) (A3 is odd in t)
    int forced=0, feasfail=0, still[600]; int ns=0;
    #pragma omp parallel for reduction(+:forced,feasfail) schedule(dynamic)
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!JOK[ip]||!JOK[im]) continue;
        if(JLo[im]-JHi[ip]<=0) continue;                     // not forced at baseline
        bool fixed=false;
        for(int d=-R;d<=R&&!fixed;d++){ i64 l1,h1,l2,h2;
            winA3(a,d,l1,h1); if(l1>h1) continue;
            winA3(-a,odd?-d:d,l2,h2); if(l2>h2) continue;
            if(std::max(l1,l2)<=std::min(h1,h2)) fixed=true; }
        if(!fixed) forced++; }
    printf("A3 offset in [-%d,%d] (%s): forced-D |t| that CANNOT be made symmetric: %d of 45\n",R,R,odd?"odd in t":"same both sides",forced);
    // which survive
    printf("survivors:");
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!JOK[ip]||!JOK[im])continue; if(JLo[im]-JHi[ip]<=0)continue;
        bool fixed=false;
        for(int d=-R;d<=R&&!fixed;d++){ i64 l1,h1,l2,h2; winA3(a,d,l1,h1); if(l1>h1)continue; winA3(-a,odd?-d:d,l2,h2); if(l2>h2)continue;
            if(std::max(l1,l2)<=std::min(h1,h2))fixed=true; }
        if(!fixed) printf(" %d",a); }
    puts("");
    return 0; }
