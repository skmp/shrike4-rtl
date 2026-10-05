// w3g_wtable.cpp -- companion to w3f: per-alignment-w breakdown, which PAIRS of pins are reachable,
// and the score when the constant is NOT fitted (C = round(KC * 2^(s1+s2-2w-15)) exactly).
#include "w3_common.h"
#include <vector>
#include <cmath>
#include <cstring>
struct Cfg { int w,s1,s2,c1,c2,r1,r2; i64 C; };
static inline i64 lat(i64 v,int u,int m){ return m? sm(v,u) : (v>>u); }
static inline i64 Jof(const Cfg&g,int t){ i64 dx=(i64)t<<g.w;
    i64 I1=lat((g.r1? arr(dx,g.C):arr(g.C,dx))+K,g.s1,g.c1);
    return lat((g.r2? arr(dx,I1):arr(I1,dx))+K,g.s2,g.c2); }
int main(){ if(!load_fsca(FD))return 1; build_tables(); if(!load_jwin())return 2;
    const i64 T17=45617,T342=18466836,TM1=18466858,TM2=18466867;
    int bexact[23], bfree[23]; Cfg cex[23], cfr[23];
    long long pairAB=0,pairAC=0,pairBC=0;
    for(int w=6;w<=22;w++){ bexact[w]=-1; bfree[w]=-1; }
    #pragma omp parallel for schedule(dynamic) reduction(+:pairAB,pairAC,pairBC)
    for(int w=6;w<=22;w++){
      int be=-1,bf=-1; Cfg ge{},gf{};
      for(int s1=6;s1<=42;s1++) for(int s2=6;s2<=42;s2++){
        long double Ce=(long double)KC*powl(2.0L,(long double)(s1+s2-2*w-15));
        if(Ce<256.0L||Ce>67108864.0L) continue; i64 C0i=(i64)llroundl(Ce);
        for(i64 C=C0i-64;C<=C0i+64;C++){ if(C<=0)continue;
          for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int c1=0;c1<2;c1++) for(int c2=0;c2<2;c2++){
            Cfg g{w,s1,s2,c1,c2,r1,r2,C};
            i64 dxm=(i64)512<<w; if((r1?dxm:C) >= (1ll<<27)) continue;
            i64 i1a=lat((r1?arr((i64)511<<w,C):arr(C,(i64)511<<w))+K,s1,c1);
            i64 i1b=lat((r1?arr(-dxm,C):arr(C,-dxm))+K,s1,c1);
            i64 r2m=r2?dxm:std::max(llabs((long long)i1a),llabs((long long)i1b)); if(r2m>=(1ll<<27))continue;
            i64 j17=Jof(g,17),j342=Jof(g,342),jm=Jof(g,-342);
            bool A=j17==T17,B=j342==T342,Cc=(jm>=TM1&&jm<=TM2);
            if(A&&B)pairAB++; if(A&&Cc)pairAC++; if(B&&Cc)pairBC++;
            if(llabs((long long)(j17-T17))>100||llabs((long long)(j342-T342))>2000) continue;
            int sc=0; for(int t=-512;t<=511;t++){ i64 j=Jof(g,t); if(j>=JLf[t+512]&&j<=JHf[t+512])sc++; }
            if(sc>bf){bf=sc;gf=g;} if(C==C0i&&sc>be){be=sc;ge=g;}
          } } }
      #pragma omp critical
      { bexact[w]=be; cex[w]=ge; bfree[w]=bf; cfr[w]=gf; }
    }
    printf("pin PAIRS reachable over the whole scan:\n");
    printf("  {J(17),J(342)}  (the decisive pair): %lld\n  {J(17),J(-342)}: %lld\n  {J(342),J(-342)}: %lld\n\n",pairAB,pairAC,pairBC);
    printf(" w  |  best score, C NOT fitted (C=round(KC*2^(s1+s2-2w-15)))  |  best score, C free in +-64\n");
    for(int w=6;w<=22;w++){
      printf("%2d  |  %4d   s1=%2d s2=%2d C=%-10lld r=(%d,%d) l=(%d,%d)   |  %4d   s1=%2d s2=%2d C=%-10lld r=(%d,%d) l=(%d,%d)\n",
        w, bexact[w], cex[w].s1,cex[w].s2,(long long)cex[w].C,cex[w].r1,cex[w].r2,cex[w].c1,cex[w].c2,
           bfree[w],  cfr[w].s1,cfr[w].s2,(long long)cfr[w].C,cfr[w].r1,cfr[w].r2,cfr[w].c1,cfr[w].c2);
    }
    return 0; }
