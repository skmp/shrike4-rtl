// we_sg.cpp -- WHERE IS THE SIGN APPLIED in the square path?
// The patent's constant is -2*pi^2, so the chain's product may be NEGATIVE throughout; the aligner then
// takes the magnitude by one's complement, i.e. sm() truncates (|P|-1).  That changes the map latch->T2
// and hence the whole allowed-latch window table.  Enumerate the placements and recompute
//   feasible t, #|t| whose +/- windows are disjoint (45 now), the even-latch ceiling (977 now).
#include "we_win.h"
#include <cstring>
#include <algorithm>
// T2 contribution to V, given the signed latch L and the table sine S at unit u.
// mode 0: V += -sm(arr(L,S)+K,u)        L>=0   (baseline)
// mode 1: V += +sm(arr(L,S)+K,u)        L<=0   (negative latch, one's-complement magnitude)
// mode 2: V += -sm(arr(S,L)+K,u)        L>=0   (roles swapped)
// mode 3: V += +sm(arr(S,L)+K,u)        L<=0
static inline i64 T2c(int mode,i64 L,i64 S,int u){
    switch(mode){ case 0: return -sm(arr(L,S)+K,u); case 1: return  sm(arr(L,S)+K,u);
                  case 2: return -sm(arr(S,L)+K,u); default: return  sm(arr(S,L)+K,u); } }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    printf("mode | feas forcedD evenMax bigD(D>2) chainScore  D(342) allowed\n");
    for(int mode=0;mode<4;mode++){
        std::vector<i64> LL(1024),LH(1024); std::vector<char> ok(1024,0);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1; int u=U2TAB[n];
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                // need base + T2c(L) in [lo,hi]  ->  T2c(L) in [lo-base, hi-base]; T2c is DECREASING in |L|
                i64 clo=lo-base, chi=hi-base;
                // binary search on magnitude m>=0 : f(m)=T2c(mode, (mode&1)? -m : m, S,u) is non-increasing
                auto f=[&](i64 m){ return T2c(mode,(mode&1)?-m:m,T.S,u); };
                i64 a1=0,b1=12000001; while(a1<b1){ i64 mm=(a1+b1)/2; if(f(mm)<=chi) b1=mm; else a1=mm+1; }
                i64 a2=-1,b2=12000000; while(a2<b2){ i64 mm=(a2+b2+1)/2; if(f(mm)>=clo) a2=mm; else b2=mm-1; }
                if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            LL[t+512]=gl;LH[t+512]=gh;ok[t+512]=gl<=gh; }
        int feas=0;for(int i=0;i<1024;i++)feas+=ok[i];
        int forced=0,evenmax=0,bigD=0;
        for(int a=0;a<=512;a++){ int ip=a+512,im=-a+512; bool okp=a<=511&&ok[ip],okm=ok[im];
            if(a==0){evenmax+=ok[512]?1:0;continue;}
            if(okp&&okm){ i64 lo=std::max(LL[ip],LL[im]),hi=std::min(LH[ip],LH[im]);
                if(lo<=hi)evenmax+=2; else {evenmax+=1;forced++; if(LL[im]-LH[ip]>2||LL[ip]-LH[im]>2)bigD++;} }
            else if(okp||okm)evenmax+=1; }
        int chain=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 dx=(i64)t<<13;
            i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20; if(J>=LL[t+512]&&J<=LH[t+512])chain++; }
        printf("%4d | %4d %6d %7d %9d %10d   [%lld,%lld] vs [%lld,%lld]\n",mode,feas,forced,evenmax,bigD,chain,
            (long long)LL[342+512],(long long)LH[342+512],(long long)LL[-342+512],(long long)LH[-342+512]);
    }
    return 0; }
