// v41_ctrl2.cpp -- extended constant-K landmark scan: adds the two OPERAND ROLE choices at each
// multiply and relaxes the multiplicand width limit to 2^26 so w=15..20 are actually covered.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
static inline i64 fdv(i64 a,i64 b){ i64 q=a/b,r=a%b; if(r&&((r<0)!=(b<0)))q--; return q; }
static const int TT[3]={17,342,-342};
static const i64 RL[3]={45617,18466836,18466858}, RH[3]={45617,18466836,18466867};
static const i64 WLIM=1ll<<26;
static inline i64 A1f(int role,i64 kc,i64 dx){ return role? arr(dx,kc) : arr(kc,dx); }
static inline i64 A2f(int role,i64 I1,i64 dx){ return role? arr(dx,I1) : arr(I1,dx); }
static int depth(int w,int s1,int s2,i64 kc,int r1role,int r2role){
    i64 P1=(i64)1<<s1,P2=(i64)1<<s2; i64 A1[3],q1[3],r1[3];
    for(int i=0;i<3;i++){ i64 dx=(i64)TT[i]<<w; if(dx>WLIM||dx<-WLIM)return -1;
        A1[i]=A1f(r1role,kc,dx); q1[i]=fdv(A1[i],P1); i64 m=A1[i]-q1[i]*P1; r1[i]=(m==0)?P1:(P1-m); }
    int best=0;
    for(int b=0;b<8;b++){ i64 klo=0,khi=P1-1,k2lo=0,k2hi=P2-1; int d=0;
        for(int i=0;i<3;i++){ int up=(b>>i)&1; i64 I1=q1[i]+up;
            if(up) klo=std::max(klo,r1[i]); else khi=std::min(khi,r1[i]-1);
            if(klo>khi) break;
            if(I1>=WLIM||I1<=-WLIM) break;
            i64 dx=(i64)TT[i]<<w; i64 A2=A2f(r2role,I1,dx); i64 q2=fdv(A2,P2); i64 m2=A2-q2*P2; i64 thr=(m2==0)?P2:(P2-m2);
            bool lo=(q2>=RL[i]&&q2<=RH[i]), hi=((q2+1>=RL[i]&&q2+1<=RH[i])&&(thr<P2));
            if(lo&&hi){} else if(lo){k2hi=std::min(k2hi,thr-1);} else if(hi){k2lo=std::max(k2lo,thr);} else break;
            if(k2lo>k2hi) break; d=i+1; }
        best=std::max(best,d); }
    return best;
}
int main(){ build_tables();
    long long c[4]={0,0,0,0}; long long tot=0,skip=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:c[:4],tot,skip)
    for(int w=2;w<=20;w++) for(int rr=0;rr<4;rr++){
      int r1role=rr&1, r2role=(rr>>1)&1;
      for(int s1=6;s1<=38;s1++) for(int s2=6;s2<=38;s2++){
        int e=s1+s2-2*w; if(e<0||e>44) continue;
        long double kcs=45617.0L*powl(2.0L,(long double)e)/289.0L;
        if(kcs<512.0L||kcs>1.6e7L) continue;
        i64 kc0=(i64)llroundl(kcs); i64 Wd=std::max((i64)262144,kc0>>4);
        i64 klo=std::max((i64)1,kc0-Wd),khi=std::min((i64)(1ll<<24)-1,kc0+Wd);
        for(i64 kc=klo;kc<=khi;kc++){ tot++; int d=depth(w,s1,s2,kc,r1role,r2role); if(d>=0)c[d]++; else skip++; }
      }
    }
    printf("configs evaluated: %lld (skipped for operand width: %lld)\n",tot,skip);
    printf("  t=17 fails          : %lld\n  t=17 only           : %lld\n  t=17 & t=342        : %lld\n  ALL THREE landmarks : %lld\n",c[0],c[1],c[2],c[3]);
    return 0; }
