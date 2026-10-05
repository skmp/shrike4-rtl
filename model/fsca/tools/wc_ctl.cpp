// wc_ctl.cpp -- (1) attribution controls: where can a free per-t quantity absorb the residual?
//               (2) the node-16 quadrant/reflection hypothesis, under asymmetric and symmetric squares
//               (3) the stored per-node square coefficient with the coefficient FITTED (form test, not rule test)
#include "wc_core.h"
#include <vector>
#include <algorithm>
static i64 Jchain(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
static i64 Jsym  (int t){ i64 Q=(arr((i64)t<<13,(i64)t<<13)+K)>>26; return (arr(KC,Q)+K)>>17; }
// generic V with overridable pieces
static i64 Vof(int node,int t,i64 dxo,i64 lin_off,i64 Jv,i64 t2_off,i64 C0){
    int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1; i64 dx=dxo;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K+lin_off,23-T.e1):0;
    i64 A3=((arr(((i64)(dx>>13))*(dx>>13)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 T2=T.S? sm(arr(Jv,T.S)+K,T.u2)+t2_off:0;
    return C0+s*T1-s*T3-T2; }
int main(){ wc_init();
    // ---------- (1) attribution controls ----------
    // baseline
    { int g=0; printf("baseline (chain J): %d\n", wc_score([&](int nd,int t){ int n=nd>=17?32-nd:nd;
        return TB[n].S? sm(arr(Jchain(t),TB[n].S)+K,TB[n].u2):(i64)0; },nullptr,&g)); }
    // A. free per-t J
    { i64 bJ[1024];
      for(int t=-512;t<=511;t++){ i64 J0=Jchain(t); int bv=1<<30; i64 bj=J0;
        for(i64 J=J0-400;J<=J0+400;J++){ int v=0;
          for(int nd=1;nd<=31;nd++){ int n=nd>=17?32-nd:nd; if(!TB[n].S)continue;
            i64 V=C0B[n]+LT[nd][t+512]-sm(arr(J,TB[n].S)+K,TB[n].u2);
            if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; }
          if(v<bv){bv=v;bj=J;} }
        bJ[t+512]=bj; }
      int g=0; printf("A. free per-t square latch J          : %d\n",
        wc_score([&](int nd,int t){ int n=nd>=17?32-nd:nd; return TB[n].S? sm(arr(bJ[t+512],TB[n].S)+K,TB[n].u2):(i64)0; },nullptr,&g)); }
    // B. free per-t dx used by ALL three terms
    { i64 bD[1024];
      #pragma omp parallel for schedule(dynamic)
      for(int t=-512;t<=511;t++){ i64 d0=(i64)t<<13; int bv=1<<30; i64 bd=d0;
        for(i64 d=d0-96;d<=d0+96;d++){ int v=0;
          for(int nd=1;nd<=31;nd++){ int n=nd>=17?32-nd:nd; if(!TB[n].S)continue;
            i64 I1=(arr(KC,d)+K)>>23, J=(arr(I1,d)+K)>>20;
            i64 V=Vof(nd,t,d,0,J,0,C0B[n]);
            if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; }
          if(v<bv){bv=v;bd=d;} }
        bD[t+512]=bd; }
      int tot=0; for(int n=0;n<=16;n++){ int nodes[2]={n,32-n},nn=(n==0||n==16)?1:2; int bv=1<<30;
        for(i64 c=C0LO[n];c<=C0HI[n];c++){ int v=0; for(int i=0;i<nn;i++){ int nd=nodes[i];
          for(int t=-512;t<=511;t++){ i64 d=bD[t+512]; i64 I1=(arr(KC,d)+K)>>23, J=(arr(I1,d)+K)>>20;
            i64 V=Vof(nd,t,d,0,J,0,c); if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; } }
          if(v<bv)bv=v; } tot+=bv; }
      printf("B. free per-t dx (shared by all 3 terms)  : %d\n",tot); }
    // C. free per-t offset on the LINEAR array output (before the shift)
    { i64 bL[1024];
      #pragma omp parallel for schedule(dynamic)
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20;
        int bv=1<<30; i64 bl=0;
        for(i64 L=-(1<<22);L<=(1<<22);L+=1024){ int v=0;
          for(int nd=1;nd<=31;nd++){ int n=nd>=17?32-nd:nd; if(!TB[n].S)continue;
            i64 V=Vof(nd,t,dx,L,J,0,C0B[n]); if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; }
          if(v<bv){bv=v;bl=L;} }
        bL[t+512]=bl; }
      int tot=0; for(int n=0;n<=16;n++){ int nodes[2]={n,32-n},nn=(n==0||n==16)?1:2; int bv=1<<30;
        for(i64 c=C0LO[n];c<=C0HI[n];c++){ int v=0; for(int i=0;i<nn;i++){ int nd=nodes[i];
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20;
            i64 V=Vof(nd,t,dx,bL[t+512],J,0,c); if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; } }
          if(v<bv)bv=v; } tot+=bv; }
      printf("C. free per-t offset in the LINEAR product: %d\n",tot); }
    // ---------- (2) node-16 quadrant / reflection ----------
    puts("\n(2) node 16 at the quadrant boundary  (sin(pi/2+d) = sin(pi/2-d))");
    for(int sym=0;sym<2;sym++){ auto Jf = sym? Jsym : Jchain;
      for(int mode=0;mode<3;mode++){
        // mode 0: as is ; 1: node16 t>=0 uses J(-t) ; 2: node16 t<0 uses J(-t)
        int v16=0; i64 bestC=0; int bv=1<<30;
        for(i64 c=C0LO[16];c<=C0HI[16];c++){ int v=0;
          for(int t=-512;t<=511;t++){ int tu=t;
            if(mode==1&&t>=0) tu=-t; if(mode==2&&t<0) tu=-t;
            i64 V=c-sm(arr(Jf(tu),TB[16].S)+K,TB[16].u2);
            if(V<WLO[16][t+512]||V>WHI[16][t+512])v++; }
          if(v<bv){bv=v;bestC=c;} }
        v16=bv; printf("   %s square, mode %d: node-16 violations %d (C0=%lld)\n",
            sym?"symmetric":"chain    ",mode,v16,(long long)bestC); } }
    // per-node even-T2 floor with node-16 reflection (t>=0 mirrors t<0): recompute floor (B)
    { int fl=0; for(int node=1;node<32;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue;
        for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512;
          i64 lo=std::max(C0B[n]+LT[node][ip]-WHI[node][ip], C0B[n]+LT[node][im]-WHI[node][im]);
          i64 hi=std::min(C0B[n]+LT[node][ip]-WLO[node][ip], C0B[n]+LT[node][im]-WLO[node][im]);
          if(lo>hi) fl++; } }
      printf("   per-node t-even T2 floor over all nodes: %d ; excluding node 16: %d\n",fl,fl-3); }
    // ---------- (3) stored per-node square coefficient, coefficient FITTED ----------
    puts("\n(3) stored per-node square coefficient C2 (form test with C2 fitted per node)");
    for(int form=0;form<2;form++)   // 0: T2 = sm(arr(C2,t^2)+K,u)   1: chain (C2*dx)>>s then *dx
      for(int du=-1;du<=1;du++){
        int tot=0; int worst16=0;
        for(int n=0;n<=16;n++){ int nodes[2]={n,32-n},nn=(n==0||n==16)?1:2;
          if(!TB[n].S){ continue; }
          int bv=1<<30;
          i64 C20=CF[2][n].v; int u0=CF[2][n].ub+du;
          for(i64 C2=C20-40;C2<=C20+40;C2++) for(i64 c=C0LO[n];c<=C0HI[n];c++){ int v=0;
            for(int i=0;i<nn;i++){ int nd=nodes[i]; int flip=nd>=17; i64 s=flip?-1:1;
              for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T2;
                if(form==0){ i64 Q=(arr(dx,dx)+K)>>26; T2=sm(arr(C2,Q)+K,u0); }
                else { i64 a=(arr(C2,dx)+K)>>(26-du); T2=sm(arr(a,dx)+K,u0); }
                const Tab&T=TB[n];
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 V=c+s*T1-s*T3-T2;
                if(V<WLO[nd][t+512]||V>WHI[nd][t+512])v++; } }
            if(v<bv)bv=v; }
          tot+=bv; if(n==16) worst16=bv; }
        printf("   form %d du=%+d : total over nodes 1..31 = %d (node 16 alone %d)\n",form,du,tot,worst16); }
    return 0; }
