// w3f_width.cpp -- BRANCH 3 (a/b) + USER "one more bit of precision": the WEIGHT OF t's LSB in the operand
// register is free, and the stored constant is re-derived for that weight.  Earlier scans fixed one or the
// other: m2_tform swept dx = t<<w with KC HELD FIXED; b26/b38/z3 swept the constants with w = 13 FIXED.
// The array's loss depends on (rec mod 2^18, mul mod 2^16), so a different w is genuinely a different
// problem -- with dx = t<<w only the low (16-w) bits of t reach the first multiply's truncated columns.
//   dx = t << w ; P1 = arr in role r1 of (C, dx) ; I1 = latch(P1 + K, s1)
//                 P2 = arr in role r2 of (I1, dx) ; J  = latch(P2 + K, s2)
//   scale law: C * 2^(2w - s1 - s2) = KC / 2^15 = 157.884...
#include "w3_common.h"
#include <vector>
#include <algorithm>
#include <cmath>
struct Cfg { int w,s1,s2,c1,c2,r1,r2; i64 C; };
static inline i64 lat(i64 v,int u,int mode){ return mode? sm(v,u) : (v>>u); }
static inline i64 Jof(const Cfg&g,int t){
    i64 dx=(i64)t<<g.w;
    i64 P1 = g.r1? arr(dx,g.C) : arr(g.C,dx);
    i64 I1 = lat(P1+K,g.s1,g.c1);
    i64 P2 = g.r2? arr(dx,I1) : arr(I1,dx);
    return lat(P2+K,g.s2,g.c2); }
int main(){ if(!load_fsca(FD))return 1; build_tables(); if(!load_jwin())return 2;
    const i64 T17=45617, T342=18466836, TM1=18466858, TM2=18466867;
    long long tried=0, valid=0, h17=0, h342=0, hm=0, p2=0, p3=0;
    std::vector<Cfg> cand, pin2;
    int bestsc=918; Cfg bestcfg{13,23,18,0,0,0,0,KC};
    #pragma omp parallel for schedule(dynamic) reduction(+:tried,valid,h17,h342,hm,p2,p3)
    for(int w=6;w<=22;w++){
      std::vector<Cfg> lc, lp;
      for(int s1=6;s1<=42;s1++) for(int s2=6;s2<=42;s2++){
        long double Ce = (long double)KC * powl(2.0L,(long double)(s1+s2-2*w-15));
        if(Ce < 256.0L || Ce > 67108864.0L) continue;
        i64 C0i=(i64)llroundl(Ce);
        for(i64 C=C0i-64;C<=C0i+64;C++){ if(C<=0) continue;
          for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int c1=0;c1<2;c1++) for(int c2=0;c2<2;c2++){
            Cfg g{w,s1,s2,c1,c2,r1,r2,C}; tried++;
            // operand-width validity: whichever operand is Booth-recoded must fit the 14-digit array
            i64 dxm=(i64)512<<w; i64 recmax = r1? dxm : C; if(recmax >= (1ll<<27)) continue;
            i64 I1p=Jof(g,0); (void)I1p;
            i64 i1a = lat((r1?arr((i64)511<<w,C):arr(C,(i64)511<<w))+K,s1,c1);
            i64 i1b = lat((r1?arr(-((i64)512<<w),C):arr(C,-((i64)512<<w)))+K,s1,c1);
            i64 rec2max = r2? dxm : std::max(llabs((long long)i1a),llabs((long long)i1b));
            if(rec2max >= (1ll<<27)) continue;
            valid++;
            i64 j17=Jof(g,17), j342=Jof(g,342), jm=Jof(g,-342);
            int pins=(j17==T17)+(j342==T342)+(jm>=TM1&&jm<=TM2);
            if(j17==T17)h17++; if(j342==T342)h342++; if(jm>=TM1&&jm<=TM2)hm++;
            if(pins>=2){p2++; if(pins>=3)p3++; lp.push_back(g);}
            if(llabs((long long)(j17-T17))<=100 && llabs((long long)(j342-T342))<=2000) lc.push_back(g);
          } } }
      #pragma omp critical
      { cand.insert(cand.end(),lc.begin(),lc.end()); pin2.insert(pin2.end(),lp.begin(),lp.end()); }
    }
    printf("scan: %lld configs enumerated, %lld operand-width-valid\n",tried,valid);
    printf("  hit J(17)=45617:        %lld\n  hit J(342)=18466836:    %lld\n  hit J(-342) window:     %lld\n",h17,h342,hm);
    printf("  hit >=2 pins: %lld    hit all 3 pins: %lld\n", p2, p3);
    printf("  near-miss candidates to full-score: %zu\n", cand.size());
    for(auto&g:pin2) cand.push_back(g);
    std::sort(cand.begin(),cand.end(),[](const Cfg&a,const Cfg&b){return std::tie(a.w,a.s1,a.s2,a.C,a.r1,a.r2,a.c1,a.c2)<std::tie(b.w,b.s1,b.s2,b.C,b.r1,b.r2,b.c1,b.c2);});
    cand.erase(std::unique(cand.begin(),cand.end(),[](const Cfg&a,const Cfg&b){return std::tie(a.w,a.s1,a.s2,a.C,a.r1,a.r2,a.c1,a.c2)==std::tie(b.w,b.s1,b.s2,b.C,b.r1,b.r2,b.c1,b.c2);}),cand.end());
    printf("  unique to full-score: %zu\n", cand.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t i=0;i<cand.size();i++){
        const Cfg&g=cand[i]; int sc=0;
        for(int t=-512;t<=511;t++){ i64 j=Jof(g,t); if(j>=JLf[t+512]&&j<=JHf[t+512]) sc++; }
        #pragma omp critical
        if(sc>bestsc){bestsc=sc;bestcfg=g;}
    }
    printf("\nBEST full jwin18 score over the scan: %d / 1024  (baseline 918)\n", bestsc);
    printf("  w=%d s1=%d s2=%d C=%lld(0x%llx) roles=(%s,%s) latches=(%s,%s)\n", bestcfg.w,bestcfg.s1,bestcfg.s2,
        (long long)bestcfg.C,(unsigned long long)bestcfg.C, bestcfg.r1?"arr(dx,C)":"arr(C,dx)",
        bestcfg.r2?"arr(dx,I1)":"arr(I1,dx)", bestcfg.c1?"sm":">>", bestcfg.c2?"sm":">>");
    printf("  J(17)=%lld J(342)=%lld J(-342)=%lld\n",(long long)Jof(bestcfg,17),(long long)Jof(bestcfg,342),(long long)Jof(bestcfg,-342));
    return 0; }
