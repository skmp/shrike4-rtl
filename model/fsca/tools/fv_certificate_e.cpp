// fv_certificate_e.cpp -- the two-sided pinned-offset test for the chain, EXHAUSTIVE in both constants.
// I1 = latch(arr1 + K1, s1) + p ;  J = latch(arr2 + K2, s2) + q ;  K1, K2 swept over their FULL periods
// by breakpoints (each latch takes exactly 2 values over a period, so 3 sample points per offset pair
// cover every reachable combination).  Integer throughout.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <set>
#include <string>
#include <cstring>
static inline i64 latch(i64 P,int s,int rule){ if(rule==0) return P>>s;
    if(rule==1) return P>=0? (P>>s) : -((-P)>>s);
    return P>=0? (P>>s) : -((~P)>>s); }
static inline i64 mod2(i64 a,int s){ i64 m=(1ll<<s); i64 r=a%m; return r<0?r+m:r; }

int main(){ build_tables();
    // pinned targets from the fine-scale dataset (re-derived in fv_certificate_b: identical to jwin18.txt)
    const i64 J17=45617, J342=18466836, Jm342lo=18466858, Jm342hi=18466867;

    // ---- part 1: can any chain give J(+17) = J(-17) = 45617, with K1,K2 exhausted, no bias?
    long long cfg=0, hitboth=0, hitsym=0, hitone=0;
    std::set<std::string> ex;
    for(int g=8;g<=14;g++) for(int rule=0;rule<3;rule++) for(int ro1=0;ro1<2;ro1++) for(int ro2=0;ro2<2;ro2++){
        i64 dxp=(i64)17<<g, dxm=-((i64)17<<g);
        i64 A1p=ro1?arr(KC,dxp):arr(dxp,KC), A1m=ro1?arr(KC,dxm):arr(dxm,KC);
        for(int s1=8;s1<=34;s1++){
            i64 k1c[3]={0,mod2(-A1p,s1),mod2(-A1m,s1)};
            for(int i=0;i<3;i++){ i64 K1=k1c[i]; i64 I1p=latch(A1p+K1,s1,rule), I1m=latch(A1m+K1,s1,rule);
                i64 A2p=ro2?arr(dxp,I1p):arr(I1p,dxp), A2m=ro2?arr(dxm,I1m):arr(I1m,dxm);
                for(int s2=8;s2<=26;s2++){ i64 k2c[3]={0,mod2(-A2p,s2),mod2(-A2m,s2)};
                    for(int j=0;j<3;j++){ i64 K2=k2c[j]; cfg++;
                        i64 Jp=latch(A2p+K2,s2,rule), Jm=latch(A2m+K2,s2,rule);
                        if(Jp==J17&&Jm==J17){ hitboth++; char b[128];
                            snprintf(b,sizeof b,"g=%d rule=%d roles=%d%d s1=%d s2=%d K1=%lld K2=%lld",g,rule,ro1,ro2,s1,s2,(long long)K1,(long long)K2); ex.insert(b); }
                        else if(Jp==J17||Jm==J17) hitone++;
                        if(Jp==Jm) hitsym++; } } } } }
    printf("[E1] %lld (g,rule,roles,s1,s2,K1,K2) samples covering every reachable constant pair\n",cfg);
    printf("     J(+17)=J(-17)=45617 : %lld   one of the two only: %lld   J(+17)==J(-17) at all: %lld\n",
           hitboth,hitone,hitsym);
    { int n=0; for(auto&s:ex){ if(n++<8) printf("       %s\n",s.c_str()); } if(ex.size()>8) printf("       ... %zu distinct configs\n",ex.size()); }

    // ---- part 2: with a free integer bias q on J the absolute value is free, so test the DIFFERENCES
    //      J(342)-J(17) = 18421219 and J(-342)-J(342) in [22,31] -- and p a bias on I1.
    printf("\n[E2] three pinned offsets via differences (q free), p on I1, KC+-64:\n");
    struct Sol{int g,s1,s2,rule,ro1,ro2,p;i64 kc;};
    std::vector<Sol> sols; long long tried=0;
    #pragma omp parallel for collapse(2) schedule(dynamic)
    for(int g=8;g<=14;g++) for(int s1=8;s1<=32;s1++){
        std::vector<Sol> loc; long long tr=0;
        for(int rule=0;rule<3;rule++) for(int ro1=0;ro1<2;ro1++) for(int ro2=0;ro2<2;ro2++)
        for(i64 kc=KC-64;kc<=KC+64;kc++){
            const int TS[3]={17,342,-342}; i64 dx[3],A1[3];
            for(int a=0;a<3;a++){ dx[a]=(i64)TS[a]<<g; A1[a]=ro1?arr(kc,dx[a]):arr(dx[a],kc); }
            i64 k1c[4]={0,mod2(-A1[0],s1),mod2(-A1[1],s1),mod2(-A1[2],s1)};
            for(int i=0;i<4;i++){ i64 K1=k1c[i]; i64 L[3]; for(int a=0;a<3;a++) L[a]=latch(A1[a]+K1,s1,rule);
                for(int p=-4;p<=4;p++){ i64 A2[3];
                    for(int a=0;a<3;a++){ i64 I=L[a]+p; A2[a]=ro2?arr(dx[a],I):arr(I,dx[a]); }
                    for(int s2=8;s2<=24;s2++){ i64 k2c[4]={0,mod2(-A2[0],s2),mod2(-A2[1],s2),mod2(-A2[2],s2)};
                        for(int j=0;j<4;j++){ i64 K2=k2c[j]; tr++;
                            i64 Jv[3]; for(int a=0;a<3;a++) Jv[a]=latch(A2[a]+K2,s2,rule);
                            if(Jv[1]-Jv[0]!=J342-J17) continue;
                            i64 D=Jv[2]-Jv[1]; if(D<Jm342lo-J342||D>Jm342hi-J342) continue;
                            loc.push_back(Sol{g,s1,s2,rule,ro1,ro2,p,kc}); } } } } }
        #pragma omp critical
        { sols.insert(sols.end(),loc.begin(),loc.end()); tried+=tr; } }
    printf("     %lld configurations tested ; %zu hit all three pinned offsets\n",tried,sols.size());
    { std::set<std::string> uniq[3]; char b[160];
      for(auto&s:sols){ snprintf(b,sizeof b,"g=%d s1=%d s2=%d roles=%d%d p=%+d KC=%lld",s.g,s.s1,s.s2,s.ro1,s.ro2,s.p,(long long)s.kc);
                        uniq[s.rule].insert(b); }
      for(int r=0;r<3;r++){ std::set<int> ps; int small=0;
        for(auto&x:uniq[r]){ int pv=0; const char*q=strstr(x.c_str(),"p="); if(q)pv=atoi(q+2); ps.insert(pv); if(pv>=-1&&pv<=1)small++; }
        printf("     latch rule %d (%s): %zu DISTINCT configurations hit all three ; p values:",r,
               r==0?"floor":(r==1?"toward zero":"ones-complement"),uniq[r].size());
        for(int v:ps)printf(" %+d",v); printf("  ; with |p|<=1: %d\n",small);
        int n=0; for(auto&x:uniq[r]) if(n++<3) printf("        %s\n",x.c_str()); } }
    return 0; }
