// v22_joint.cpp -- INDEPENDENT exact test: is there ANY (w,s1,s2,KCw,K1,K2) with
//     I1 = (arr(KCw, t<<w) + K1) >> s1 ;  J = (arr(I1, t<<w) + K2) >> s2
// such that J(17) = 45617 AND J(342) = 18466836 simultaneously?
// Exact, not sampled: for a given KCw, K1 in [0,2^s1) makes I1 take at most 2 consecutive values at
// each offset, so the (a,b) pair space is tiny and each pair pins K1 and K2 to intervals which are
// intersected in closed form.  KCw is swept over a bracket derived FROM THE TARGET and certified by
// its endpoints.  Operands that leave the array's signed 28-bit recoder range are rejected, not scored.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
static const i64 TA=45617, TBB=18466836;
struct Res{ long long trip, certA, certB, hitA, hitB, hitBoth, rejI1; };
static inline i64 fdiv_sh(i64 v,int s){ return v>>s; }   // arithmetic shift, matches the model

int main(int argc,char**argv){ build_tables();
    int LOOSE = argc>1 ? atoi(argv[1]) : 0;   // 0: K2 in [0,2^s2) (a real rounding constant); 1: K2 any integer
    Res R{0,0,0,0,0,0,0};
    std::vector<std::string> wins;
    long long pairs_tested=0;
    for(int w=6;w<=18;w++){
        i64 dxA=(i64)17<<w, dxB=(i64)342<<w;
        if(llabs((i64)512<<w)>=(1ll<<27)) continue;                  // dx must fit the operand path
        for(int s2=(w>4?w-4:1);s2<=w+24;s2++) for(int s1=4;s1<=2*w+30;s1++){
            i64 per1=1ll<<s1, per2=1ll<<s2;
            long double ctr=(long double)TBB*powl(2.0L,(long double)(s1+s2))/((long double)dxB*(long double)dxB);
            if(ctr<16384.0L||ctr>=134217728.0L) continue;
            i64 c0=(i64)llroundl(ctr); i64 span=(i64)(ctr*1e-5L)+2048;
            i64 lo=c0-span, hi=c0+span; if(lo<1)lo=1;
            // reject the triple outright if the first latch leaves the recoder range anywhere in the bracket
            i64 I1lo=(arr(hi,dxB)+per1-1)>>s1;
            if(llabs(I1lo)>=(1ll<<27)){ R.rejI1++; continue; }
            R.trip++;
            bool certA=false, certB=false, gotA=false, gotB=false, gotBoth=false;
            // certification: the bracket endpoints must straddle each target
            { i64 aL=(arr(lo,dxA))>>s1, aH=(arr(hi,dxA)+per1-1)>>s1;
              i64 jAL=(arr(aL,dxA)+(LOOSE?0:0))>>s2, jAH=(arr(aH,dxA)+per2-1)>>s2;
              certA = jAL<=TA && jAH>=TA;
              i64 bL=(arr(lo,dxB))>>s1, bH=(arr(hi,dxB)+per1-1)>>s1;
              i64 jBL=(arr(bL,dxB))>>s2, jBH=(arr(bH,dxB)+per2-1)>>s2;
              certB = jBL<=TBB && jBH>=TBB; }
            if(certA) R.certA++; if(certB) R.certB++;
            for(i64 kc=lo;kc<=hi;kc++){
                i64 PA=arr(kc,dxA), PB=arr(kc,dxB);
                // enumerate the (a,b) pairs reachable with a single K1 in [0,2^s1)
                i64 a0=PA>>s1, a1=(PA+per1-1)>>s1;
                for(i64 a=a0;a<=a1;a++){
                    i64 k1lo=a*per1-PA, k1hi=a*per1+per1-1-PA;
                    if(k1lo<0)k1lo=0; if(k1hi>per1-1)k1hi=per1-1; if(k1lo>k1hi) continue;
                    i64 b0=(PB+k1lo)>>s1, b1=(PB+k1hi)>>s1;
                    for(i64 b=b0;b<=b1;b++){
                        i64 j1=b*per1-PB, j2=b*per1+per1-1-PB;
                        i64 kl=std::max(k1lo,j1), kh=std::min(k1hi,j2); if(kl>kh) continue;
                        if(llabs(a)>=(1ll<<27)||llabs(b)>=(1ll<<27)) continue;   // recoder range
                        pairs_tested++;
                        i64 QA=arr(a,dxA), QB=arr(b,dxB);
                        i64 lA=TA*per2-QA,  hA=TA*per2+per2-1-QA;
                        i64 lB=TBB*per2-QB, hB=TBB*per2+per2-1-QB;
                        bool okA = LOOSE ? true : (lA<=per2-1 && hA>=0);
                        bool okB = LOOSE ? true : (lB<=per2-1 && hB>=0);
                        if(okA) gotA=true; if(okB) gotB=true;
                        i64 L=std::max(lA,lB), H=std::min(hA,hB);
                        if(!LOOSE){ L=std::max(L,(i64)0); H=std::min(H,per2-1); }
                        if(L<=H){ gotBoth=true;
                            if(wins.size()<8){ char buf[256];
                              snprintf(buf,sizeof buf,"w=%d s1=%d s2=%d KCw=%lld K1=%lld K2=%lld (a=%lld b=%lld)",
                                       w,s1,s2,(long long)kc,(long long)kl,(long long)L,(long long)a,(long long)b);
                              wins.push_back(buf); } }
                    } } }
            if(gotA)R.hitA++; if(gotB)R.hitB++; if(gotBoth)R.hitBoth++; } }
    printf("=== joint landmark reachability, K2 %s ===\n", LOOSE?"UNCONSTRAINED (any integer)":"in [0,2^s2) (a real rounding constant)");
    printf("triples scanned                : %lld   (rejected for I1 >= 2^27: %lld)\n",R.trip,R.rejI1);
    printf("bracket certified for t=17     : %lld\n",R.certA);
    printf("bracket certified for t=342    : %lld\n",R.certB);
    printf("(a,b,K1,K2) quadruples tested  : %lld\n",pairs_tested);
    printf("triples reaching J(17)=45617   : %lld\n",R.hitA);
    printf("triples reaching J(342)=18466836: %lld\n",R.hitB);
    printf("triples reaching BOTH          : %lld\n",R.hitBoth);
    for(auto&s:wins) printf("   WIN: %s\n",s.c_str());
    return 0; }
