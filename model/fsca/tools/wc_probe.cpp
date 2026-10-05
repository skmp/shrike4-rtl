// wc_probe.cpp -- targeted probe of the 2-stage chain across the FIRST latch shift s0 (which sets how
// coarse the intermediate is) and the latch rule, since the residual analysis asks for an intermediate
// whose LSB is worth >= 6 J units at |t| ~ 228 (i.e. s0 >= 25) with a near-symmetric truncation.
#include "wc_core.h"
#include <vector>
#include <algorithm>
static i64 JLo[1024],JHi[1024]; static char JOK[1024];
static void build_jwin(){
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue; const Tab&T=TB[n];
            i64 base=C0B[n]+LT[node][t+512];
            i64 t2lo=base-WHI[node][t+512], t2hi=base-WLO[node][t+512]; int u=T.u2;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLo[t+512]=gl;JHi[t+512]=gh;JOK[t+512]=gl<=gh; } }
static inline i64 MUL(i64 a,i64 b,int r){ return r?arr(b,a):arr(a,b); }
int main(){ wc_init(); build_jwin();
    int nf=0; for(int i=0;i<1024;i++)nf+=JOK[i];
    puts("s0 s1 rule0 rule1 ro0 ro1 c0 c1 | jwin  model guard");
    struct Best{int j,m,g,ro0,ro1,c0,c1,r1;};
    for(int s0=18;s0<=30;s0++){ int s1=43-s0;
      for(int r0=0;r0<3;r0++){ Best bb{-1,1<<30,0,0,0,0,0,0};
        for(int r1=0;r1<3;r1++) for(int ro0=0;ro0<2;ro0++) for(int ro1=0;ro1<2;ro1++)
        for(int c0=0;c0<2;c0++) for(int c1=0;c1<2;c1++){
          i64 J[1024];
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 I=wlatch(MUL((i64)KC,dx,ro0)+(c0?K:0),s0,r0);
            J[t+512]=wlatch(MUL(I,dx,ro1)+(c1?K:0),s1,r1); }
          int js=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue;
            if(J[t+512]>=JLo[t+512]&&J[t+512]<=JHi[t+512])js++; }
          int g=0; int m=wc_score([&](int nd,int t)->i64{ int n=nd>=17?32-nd:nd; if(!TB[n].S)return 0;
              return sm(arr(J[t+512],TB[n].S)+K,TB[n].u2); },nullptr,&g);
          if(m<bb.m||(m==bb.m&&js>bb.j)){ bb={js,m,g,ro0,ro1,c0,c1,r1}; } }
        printf("%2d %2d   %d     %d    %d   %d   %d  %d | %4d/%d %4d  %d\n",s0,s1,r0,bb.r1,bb.ro0,bb.ro1,bb.c0,bb.c1,bb.j,nf,bb.m,bb.g); } }
    return 0; }
