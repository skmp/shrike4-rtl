// v50_ratio.cpp -- (a) the proportionality argument, measured, not asserted;
//                  (b) the p-scan diagnostic;
//                  (c) identify the measured 4th difference as sine's quartic term (turns the
//                      node-split bound from "a number" into a physically checked quantity).
#include "v50_core.h"
#include <cmath>
int main(){
    if(!load_fsca(VF)){puts("no dump");return 1;} build_tables(); if(!v50_load_jwin())return 1;
    VCfg c; c.s2=18;
    auto Jp=[&](int t,long long p){ i64 dx=((i64)t<<13)+p; return v50_J(dx,dx,c); };
    printf("(a) dJ/d(dx offset), measured at p=0 -> p=1, against the law 2*KC*dx/2^41 = KC*t/2^27\n");
    for(int t : {17,64,171,342,511}){
        long long d = Jp(t,1)-Jp(t,0);
        printf("    t=%4d : measured over 32 steps %.4f   law %.4f\n",
               t,(double)(Jp(t,32)-Jp(t,0))/32.0,(double)KC*t/134217728.0);
        (void)d; }
    printf("\n(b) uniform dx-level offset p (both square multiplies): full 1024 rescoring\n");
    printf("    %-5s %-9s %-8s %-11s %-11s %s\n","p","score","J(17)","J(342)","J(-342)","note");
    for(long long p : {-40LL,-30LL,-25LL,-20LL,-19LL,-18LL,-10LL,0LL,10LL,19LL,20LL}){
        int sc=v50_score([&](int t){return Jp(t,p);});
        printf("    %-5lld %4d/1024 %-8lld %-11lld %-11lld %s\n",p,sc,
            (long long)Jp(17,p),(long long)Jp(342,p),(long long)Jp(-342,p),
            Jp(17,p)==45617?"<- J(17) EXACT":"");
    }
    printf("    targets                45617    18466836    18466858..67\n");
    // best possible over ALL uniform p, reported honestly
    { int best=0; long long bp=0;
      for(long long p=-20000;p<=20000;p++){ int s=v50_score([&](int t){return Jp(t,p);}); if(s>best){best=s;bp=p;} }
      printf("    best over p in [-20000,20000]: %d/1024 at p=%lld (baseline p=0 is %d)\n",
             best,bp,v50_score([&](int t){return Jp(t,0);})); }
    printf("\n(c) is the measured 4th difference just sine's quartic?  D_pred = h_rad^4 * sin(c) * 2^26\n");
    printf("    %-8s %-8s %-12s %-12s %s\n","centre","h","D measured","D predicted","ratio");
    for(int c2 : {16384, 8192, 4096, 32768+16384}) for(int h : {256,512,1023}){
        static const int co[5]={1,-4,6,-4,1}; i64 dlo=0,dhi=0;
        for(int i=0;i<5;i++){ int a=(c2+(i-2)*h)&0xffff; i64 lo,hi; v50_win(a,lo,hi);
            if(co[i]>0){dlo+=co[i]*lo;dhi+=co[i]*hi;} else {dlo+=co[i]*hi;dhi+=co[i]*lo;} }
        long double hr=(long double)h*3.14159265358979323846264L/32768.0L;
        long double ang=(long double)c2*3.14159265358979323846264L/32768.0L;
        long double pred=powl(hr,4)*sinl(ang)*67108864.0L;
        long double mid=(long double)(dlo+dhi)/2.0L;
        printf("    %-8d %-8d %-12.1Lf %-12.3Lf %.4Lf\n",c2,h,mid,pred, pred!=0? mid/pred : 0.0L);
    }
    return 0; }
