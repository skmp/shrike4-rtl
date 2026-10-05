// w2_ratio.cpp -- the structural version of branch 2's negative result.  No scanning of KCw at all.
//
// In  dx = t<<w1 ; I1 = (arr(KCw,dx)+K1)>>s1 ; J = (arr(I1,dx2)+K2)>>s2   the first stage enters the
// second ONLY through the integer I1(t).  Write X = KCw*2^w1/2^s1 > 0 (a positive real once KCw is
// free).  Then, with loss in [-LMAX,0] and K1 in [0,2^s1),
//        I1(t) = floor(X|t| + (loss+K1)/2^s1)  in  [X|t| - d - 1, X|t| + 1],   d = LMAX/2^s1
// so a required value m at offset t pins X to the interval [(m-1)/|t|, (m+d+1)/|t|].
// The two pinned landmarks therefore demand first-latch values whose RATIO is 342/17, to within one
// latch step at each end -- a constraint that does not contain w1, s1, KCw or K1 at all.
// This tool enumerates, for every second-stage geometry (w2,s2), every admissible I1 at t=17 and at
// t=342 (K2 free), and asks whether any pair is ratio-compatible.  A hit would mean SOME first stage
// exists; no hit excludes every first stage at once, including non-integer and out-of-range ones.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
static const i64 CAPN=4000000;
static int adm(int t,int w2,int s2,std::vector<i64>&out){ out.clear(); i64 dx=(i64)t<<w2; int i=t+512;
    i64 ad=llabs(dx); if(!ad) return 0;
    i64 c0=(((i64)JL[i]-2)<<s2)/ad-4; if(c0<0)c0=0; i64 c1=(((i64)JH[i]+3)<<s2)/ad+4;
    i64 n=c1-c0+1; if(n>CAPN) return -1;
    for(i64 m=c0;m<=c1;m++){ i64 I1=dx>0?m:-m; i64 q=arr(I1,dx)>>s2;
        if((q>=JL[i]&&q<=JH[i])||(q+1>=JL[i]&&q+1<=JH[i])) out.push_back(m); }
    return 1; }
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    long long pairs_tested=0, combos=0, unres=0, hits=0, hits_m=0;
    long double bestslack=1e18L; int bw=0,bs2=0,bs1=0; i64 bm1=0,bm2=0;
    printf("second-stage geometries (w2,s2) x first-stage shift s1 -- s1 enters only through the loss term\n");
    for(int w2=2;w2<=24;w2++) for(int s2=4;s2<=40;s2++){
        std::vector<i64> A17,A342,Am342;
        int e1=adm(17,w2,s2,A17), e2=adm(342,w2,s2,A342), e3=adm(-342,w2,s2,Am342);
        if(e1<0||e2<0||e3<0){ unres++; continue; }
        if(A17.empty()||A342.empty()) continue;
        for(int s1=4;s1<=40;s1++){ combos++;
            long double d=(long double)LMAX/powl(2.0L,s1);
            // reachability of the magnitudes themselves: m = I1 must be < 2^28 to be recodable by the array
            bool found=false, foundm=false;
            for(i64 m1:A17){ if(m1>=KCMAX) continue;
                // X window from t=17
                long double xlo1=(long double)(m1-1)/17.0L, xhi1=(long double)(m1+d+1)/17.0L;
                for(i64 m2:A342){ if(m2>=KCMAX) continue; pairs_tested++;
                    long double xlo2=(long double)(m2-1)/342.0L, xhi2=(long double)(m2+d+1)/342.0L;
                    long double lo=std::max(xlo1,xlo2), hi=std::min(xhi1,xhi2);
                    long double slack=lo-hi;      // <=0 means compatible
                    if(slack<bestslack){ bestslack=slack; bw=w2;bs2=s2;bs1=s1;bm1=m1;bm2=m2; }
                    if(lo<=hi){ found=true;
                        for(i64 m3:Am342){ if(m3>=KCMAX) continue;
                            long double xlo3=(long double)(m3-1)/342.0L, xhi3=(long double)(m3+d+1)/342.0L;
                            if(std::max(lo,xlo3)<=std::min(hi,xhi3)) foundm=true; } } } }
            if(found){ hits++; if(foundm) hits_m++; }
        } }
    printf("(w2,s2,s1) combinations examined            : %lld\n",combos);
    printf("(I1(17), I1(342)) candidate pairs tested    : %lld\n",pairs_tested);
    printf("(w2,s2) geometries left unresolved by the cap: %lld\n",unres);
    printf("combinations where SOME first stage (any w1, s1, KCw real, K1) serves BOTH landmarks: %lld\n",hits);
    printf("   ... and t=-342 as well: %lld\n",hits_m);
    printf("closest miss: needed X windows overlap by %.6Lf (positive = gap) at w2=%d s2=%d s1=%d, I1(17)=%lld I1(342)=%lld\n",
        bestslack,bw,bs2,bs1,(long long)bm1,(long long)bm2);
    printf("   ratio check: 342*I1(17) = %lld  vs  17*I1(342) = %lld   (difference %lld, tolerance ~%.0Lf)\n",
        (long long)(342*bm1),(long long)(17*bm2),(long long)(342*bm1-17*bm2),(long double)(342+17));
    return 0; }
