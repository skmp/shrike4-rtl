// v41_scan.cpp -- INDEPENDENT joint scan over (w, s1, s2, KC) with BOTH rounding constants free
// PER OFFSET (deliberately more generous than any physical model), using the exact arr().
// Question: does any alignment hit the three pinned landmarks?
//   t=17 -> J==45617 ; t=342 -> J==18466836 ; t=-342 -> J in [18466858,18466867]
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
static inline i64 fdv(i64 a,i64 b){ i64 q=a/b,r=a%b; if(r&&((r<0)!=(b<0)))q--; return q; }

// reachable J set at offset t for given (w,s1,s2,KC) with K1 in [0,2^s1), K2 in [0,2^s2), per offset.
// returns [lo,hi] (a contiguous interval: union of two adjacent 2-value intervals)
static inline void reach(int t,int w,int s1,int s2,i64 kc,i64& lo,i64& hi,bool& ok){
    i64 dx=(i64)t<<w;
    if (dx> (1ll<<23) || dx < -(1ll<<23)) { ok=false; return; }
    if (kc >= (1ll<<24)) { ok=false; return; }
    i64 A1=arr(kc,dx);
    i64 P1=(i64)1<<s1; i64 i1a=fdv(A1,P1), i1b=fdv(A1+P1-1,P1);
    lo=(i64)1<<62; hi=-((i64)1<<62); ok=true;
    for(i64 I1=i1a;I1<=i1b;I1++){
        if (I1 >= (1ll<<24) || I1 <= -(1ll<<24)) { ok=false; return; }
        i64 A2=arr(I1,dx);
        i64 P2=(i64)1<<s2; i64 a=fdv(A2,P2), b=fdv(A2+P2-1,P2);
        lo=std::min(lo,a); hi=std::max(hi,b);
    }
}
int main(){
    build_tables();
    long long tot=0, pass17=0, pass342=0, pass3=0;
    long long tot_ws=0;
    #pragma omp parallel for schedule(dynamic) reduction(+:tot,pass17,pass342,pass3,tot_ws)
    for(int w=2;w<=17;w++){
        for(int s1=6;s1<=34;s1++) for(int s2=6;s2<=34;s2++){
            int e = s1+s2-2*w;                       // KC ~ 45617 * 2^e / 289
            if (e < 0 || e > 40) continue;
            long double kcs = 45617.0L * powl(2.0L,(long double)e) / 289.0L;
            if (kcs < 1024.0L || kcs > 1.6e7L) continue;
            i64 kc0 = (i64)llroundl(kcs);
            i64 Wd = std::max((i64)8192, kc0>>8);
            i64 klo = std::max((i64)1, kc0-Wd), khi = std::min((i64)(1ll<<24)-1, kc0+Wd);
            tot_ws++;
            bool edge_lo=false, edge_hi=false;
            for(i64 kc=klo; kc<=khi; kc++){
                tot++;
                i64 lo,hi; bool ok;
                reach(17,w,s1,s2,kc,lo,hi,ok); if(!ok) continue;
                if (kc==klo && lo<=45617 && 45617<=hi) edge_lo=true;
                if (kc==khi && lo<=45617 && 45617<=hi) edge_hi=true;
                if (!(lo<=45617 && 45617<=hi)) continue;
                pass17++;
                i64 lo2,hi2; reach(342,w,s1,s2,kc,lo2,hi2,ok); if(!ok) continue;
                if (!(lo2<=18466836 && 18466836<=hi2)) continue;
                pass342++;
                i64 lo3,hi3; reach(-342,w,s1,s2,kc,lo3,hi3,ok); if(!ok) continue;
                if (hi3<18466858 || lo3>18466867) continue;
                pass3++;
                #pragma omp critical
                printf("*** HIT w=%d s1=%d s2=%d KC=%lld\n",w,s1,s2,(long long)kc);
            }
            if (edge_lo||edge_hi) {
                #pragma omp critical
                printf("WARN window edge active at w=%d s1=%d s2=%d (lo=%d hi=%d) -- widen\n",w,s1,s2,(int)edge_lo,(int)edge_hi);
            }
        }
    }
    printf("(w,s1,s2) cells scanned: %lld ; KC values evaluated: %lld\n", tot_ws, tot);
    printf("pass t=17 : %lld\npass t=17 & 342 : %lld\npass all three : %lld\n", pass17, pass342, pass3);
    return 0;
}
