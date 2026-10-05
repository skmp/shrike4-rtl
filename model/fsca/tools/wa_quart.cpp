// wa_quart.cpp -- the quartic question.  sin(x0+d)'s d^4 term has the SAME sin(x0) coefficient as
// the d^2 term, so the hardware can fold it into the square path's latch J.  Does the required J
// actually carry a t^4 component, and is the "economised" constant just that fold?
#include "wa_common.h"
#include <omp.h>
typedef long double ld;
static Windows W; static std::vector<int> AT;
static int cover(ld a,ld c4,ld*bo=nullptr){ static thread_local std::vector<std::pair<ld,int>> ev; ev.clear();
    for(int t:AT){ ld t2=(ld)t*(ld)t; ld q=a*t2+c4*t2*t2;
        ev.push_back({(ld)W.JL[t+512]-q,1}); ev.push_back({(ld)W.JH[t+512]+1-q,-1}); }
    std::sort(ev.begin(),ev.end()); int cur=0,best=0; size_t i=0; ld bb=0;
    while(i<ev.size()){ ld x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;}
        if(cur>best){best=cur;bb=x;} } if(bo)*bo=bb; return best; }
int main(){ build_tables(); if(!load_windows(W))return 1;
    for(int t=-512;t<=511;t++) if(W.ok[t+512]) AT.push_back(t);
    ld PI=3.14159265358979323846L;
    ld a0=4*PI*PI;                                   // ideal quadratic coefficient of J
    ld c0=-PI*PI*PI*PI/(3.0L*32768.0L*32768.0L);     // ideal quartic: -4pi^2 * (pi^2/(12*32768^2))
    printf("ideal a = 4pi^2 = %.9Lf  (a*2^17 = %.2Lf)\n",a0,a0*131072);
    printf("ideal quartic coefficient c = -pi^4/(3*32768^2) = %.6Le   (at t=512: %.1Lf J units)\n",c0,c0*262144.0L*262144.0L);
    int best=0; ld ba=0,bc=0;
    #pragma omp parallel
    { int lb=0; ld la=0,lc=0;
      #pragma omp for schedule(dynamic)
      for(int ic=0;ic<=400;ic++){ ld c=c0*ic/200.0L;                 // 0 .. 2x the ideal quartic
          for(int i=0;i<=40000;i++){ ld a=39.4690L+(39.4800L-39.4690L)*i/40000;
              int s=cover(a,c); if(s>lb){lb=s;la=a;lc=c;} } }
      #pragma omp critical
      { if(lb>best){best=lb;ba=la;bc=lc;} } }
    printf("\nbest even model with a t^4 term: %d/1022 at a=%.9Lf (a*2^17=%.2Lf), c=%.4Le (=%.3Lf x ideal)\n",
        best,ba,ba*131072,bc,bc/c0);
    printf("   compare: best with c=0 was 931 at a*2^17=5173573.7\n");
    printf("   a with the ideal quartic: score %d\n",cover(a0,c0));
    // what does a pure 4pi^2 + ideal quartic predict at the pinned t?
    for(int t : {40,258,342,376,512}){ ld t2=(ld)t*(ld)t;
        printf("   t=%3d: required J %lld..%lld ; 4pi^2 t^2 = %.1Lf ; +ideal quartic = %.1Lf ; best fit = %.1Lf\n",
            t,(long long)W.JL[t+512],(long long)W.JH[t+512],a0*t2,a0*t2+c0*t2*t2,ba*t2+bc*t2*t2); }
    return 0; }
