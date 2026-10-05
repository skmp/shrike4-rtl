// wv_asym_knob.cpp -- adversarial verification, part 6: the attribution knobs of claim item 4.
// For each candidate "one free per-t knob" (shared by all node instances, as a node-independent
// datapath value must be), minimise the window violations over nodes 0..31 by exhaustive scan.
#include "wv_asym_win.h"
#include <cstdio>
#include <algorithm>
static i64 DLO[1024][32],DHI[1024][32]; static int NODES[32],NCNT;   // dump windows on V
int main(){ if(!load_fsca(WVF))return 1; build_tables();
    NCNT=0; for(int node=0;node<=31;node++) NODES[NCNT++]=node;
    for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ int a=(NODES[i]*1024+t)&0xffff; DWin w=wv_dumpwin(a);
        DLO[t+512][i]=w.lo; DHI[t+512][i]=w.hi; }
    // baseline pieces
    auto Vfull=[&](int node,int t,i64 A3ov,int useA3,i64 Jov,int useJ,i64 voff){
        int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 dx=(i64)t<<13,s=flip?-1:1;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3= useA3? A3ov : ((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 I1=(arr(KC,dx)+K)>>23; i64 Jb=(arr(I1,dx)+K)>>20; i64 J= useJ? Jov : Jb;
        i64 T2=T.S? sm(arr(J,T.S)+K,WV_U2[n]):0;
        return WV_C0[n]+s*T1-T2-s*T3+voff; };
    // 0) baseline
    { int v=0; for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ i64 V=Vfull(NODES[i],t,0,0,0,0,0);
        if(V<DLO[t+512][i]||V>DHI[t+512][i])v++; } printf("baseline (no knob)                 : %d violations\n",v); }
    // 1) free per-t common additive offset on V
    { long long tot=0;
      #pragma omp parallel for reduction(+:tot) schedule(dynamic)
      for(int t=-512;t<=511;t++){ int best=1<<30;
        for(i64 off=-40;off<=40;off++){ int v=0; for(int i=0;i<NCNT;i++){ i64 V=Vfull(NODES[i],t,0,0,0,0,off);
            if(V<DLO[t+512][i]||V>DHI[t+512][i])v++; } if(v<best)best=v; } tot+=best; }
      printf("free per-t common offset on V      : %lld violations\n",tot); }
    // 2) free per-t cubic latch A3 (unbounded: scan a window wide enough that T3 leaves its range)
    { long long tot=0; int worstedge=0;
      #pragma omp parallel for reduction(+:tot) schedule(dynamic)
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 nom=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        int best=1<<30; i64 span=40000;
        for(i64 A3=nom-span;A3<=nom+span;A3++){ int v=0; for(int i=0;i<NCNT;i++){ i64 V=Vfull(NODES[i],t,A3,1,0,0,0);
            if(V<DLO[t+512][i]||V>DHI[t+512][i])v++; } if(v<best)best=v; } tot+=best; (void)worstedge; }
      printf("free per-t cubic latch A3          : %lld violations\n",tot); }
    // 3) free per-t square latch J
    { long long tot=0;
      #pragma omp parallel for reduction(+:tot) schedule(dynamic)
      for(int t=-512;t<=511;t++){ i64 Jc=(KC*(i64)t*(i64)t)>>17; int best=1<<30;
        for(i64 J=std::max((i64)0,Jc-4000);J<=Jc+4000;J++){ int v=0; for(int i=0;i<NCNT;i++){ i64 V=Vfull(NODES[i],t,0,0,J,1,0);
            if(V<DLO[t+512][i]||V>DHI[t+512][i])v++; } if(v<best)best=v; } tot+=best; }
      printf("free per-t square latch J          : %lld violations\n",tot); }
    return 0; }
