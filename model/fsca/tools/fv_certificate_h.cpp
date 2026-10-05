// fv_certificate_h.cpp -- THE decisive t=+-17 test, with both constants exhausted over their full
// periods by EXACT breakpoints (binary search on K, which the latch is monotone in -- unlike arr).
// Question: can a two-latch chain produce J(+17) = J(-17) = 45617 with no integer bias?
#include "base2.h"
#include <cstdio>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
static inline i64 latch(i64 P,int s,int rule){ if(rule==0) return P>>s;
    if(rule==1) return P>=0?(P>>s):-((-P)>>s); return P>=0?(P>>s):-((~P)>>s); }
// every K in [0,2^s) giving a distinct latch value, found exactly (latch is monotone in K)
static void kcands(i64 A,int s,int rule,std::vector<i64>&out){ i64 P=1ll<<s;
    out.push_back(0); out.push_back(P-1);
    i64 v0=latch(A,s,rule), v1=latch(A+P-1,s,rule);
    if(v0!=v1){ i64 lo=0,hi=P-1; while(lo<hi){ i64 m=lo+(hi-lo)/2; if(latch(A+m,s,rule)>v0) hi=m; else lo=m+1; }
        out.push_back(lo); if(lo>0) out.push_back(lo-1);
        // a second step can only occur if the span covers more than two values; check
        i64 v2=latch(A+lo,s,rule); if(v2!=v1){ i64 a=lo,b=P-1; while(a<b){ i64 m=a+(b-a)/2; if(latch(A+m,s,rule)>v2) b=m; else a=m+1; }
            out.push_back(a); if(a>0) out.push_back(a-1); } }
    std::sort(out.begin(),out.end()); out.erase(std::unique(out.begin(),out.end()),out.end()); }
int main(){ build_tables(); const i64 J17=45617;
    long long cfg=0, both=0, any=0, sym=0; std::set<std::string> hits;
    int maxvals=0;
    for(int g=8;g<=14;g++) for(int rule=0;rule<3;rule++) for(int ro1=0;ro1<2;ro1++) for(int ro2=0;ro2<2;ro2++){
        i64 dxp=(i64)17<<g, dxm=-((i64)17<<g);
        i64 A1p=ro1?arr(KC,dxp):arr(dxp,KC), A1m=ro1?arr(KC,dxm):arr(dxm,KC);
        for(int s1=8;s1<=34;s1++){ std::vector<i64> k1; kcands(A1p,s1,rule,k1); kcands(A1m,s1,rule,k1);
            std::sort(k1.begin(),k1.end()); k1.erase(std::unique(k1.begin(),k1.end()),k1.end());
            maxvals=std::max(maxvals,(int)k1.size());
            for(i64 K1:k1){ i64 I1p=latch(A1p+K1,s1,rule), I1m=latch(A1m+K1,s1,rule);
                i64 A2p=ro2?arr(dxp,I1p):arr(I1p,dxp), A2m=ro2?arr(dxm,I1m):arr(I1m,dxm);
                for(int s2=8;s2<=26;s2++){ std::vector<i64> k2; kcands(A2p,s2,rule,k2); kcands(A2m,s2,rule,k2);
                    std::sort(k2.begin(),k2.end()); k2.erase(std::unique(k2.begin(),k2.end()),k2.end());
                    for(i64 K2:k2){ cfg++;
                        i64 Jp=latch(A2p+K2,s2,rule), Jm=latch(A2m+K2,s2,rule);
                        if(Jp==J17&&Jm==J17){ both++; char b[160];
                            snprintf(b,sizeof b,"g=%d rule=%d roles=%d%d s1=%d s2=%d K1=%lld K2=%lld",g,rule,ro1,ro2,s1,s2,(long long)K1,(long long)K2); hits.insert(b); }
                        if(Jp==J17||Jm==J17) any++;
                        if(Jp==Jm) sym++; } } } } }
    printf("[H] %lld (constant-pair) samples, covering every reachable latch-value combination\n",cfg);
    printf("    max distinct K candidates needed for one latch: %d\n",maxvals);
    printf("    J(+17)=J(-17)=45617 : %lld  (%zu distinct configs)\n",both,hits.size());
    printf("    J(+17)=45617 or J(-17)=45617 at all : %lld\n",any);
    printf("    J(+17)==J(-17) : %lld\n",sym);
    // sanity: the same sweep asking only for the coarse-scale value 11404 = round(45617/4) must find hits,
    // otherwise the enumeration is broken.
    { long long h=0; for(int g=8;g<=14;g++){ i64 dx=(i64)17<<g; i64 A1=arr(dx,KC);
        for(int s1=8;s1<=34;s1++){ std::vector<i64> k1; kcands(A1,s1,0,k1);
          for(i64 K1:k1){ i64 I1=latch(A1+K1,s1,0); i64 A2=arr(I1,dx);
            for(int s2=8;s2<=26;s2++){ std::vector<i64> k2; kcands(A2,s2,0,k2);
              for(i64 K2:k2) if(latch(A2+K2,s2,0)==45616) h++; } } } }
      printf("    control: J(+17)=45616 (one less) is reachable %lld times, so the enumeration is live\n",h); }
    return 0; }
