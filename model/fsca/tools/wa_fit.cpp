// wa_fit.cpp -- what smooth law does the required J actually follow?  Max window coverage for
//   (1) J = floor(a t^2 + b)              (even)
//   (2) J = floor(a t^2 + b + c|t|)       (even, with a |t| term)
//   (3) J = floor(a t^2 + b_sign)         (even + a pure sign offset)
//   (4) J = floor(a t^2 + b + c t)        (odd linear term)
// and the exact feasible a-interval of the best.
#include "wa_common.h"
#include <omp.h>
typedef long double ld;
static Windows W; static std::vector<int> AT;
static int cover(ld a,ld c,int mode,ld*bout=nullptr){   // mode 0: b only, 1: +c|t|, 2: +c*t, 3: sign-split
    static thread_local std::vector<std::pair<ld,int>> ev; ev.clear();
    if(mode==3){ int tot=0; for(int sgn=0;sgn<2;sgn++){ ev.clear();
            for(int t:AT){ if((t<0)!=(sgn==1))continue; ld q=a*(ld)t*(ld)t;
                ev.push_back({(ld)W.JL[t+512]-q,1}); ev.push_back({(ld)W.JH[t+512]+1-q,-1}); }
            std::sort(ev.begin(),ev.end()); int cur=0,best=0; size_t i=0;
            while(i<ev.size()){ ld x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;}
                if(cur>best)best=cur; } tot+=best; } return tot; }
    for(int t:AT){ ld q=a*(ld)t*(ld)t + (mode==1? c*(ld)(t<0?-t:t) : mode==2? c*(ld)t : 0);
        ev.push_back({(ld)W.JL[t+512]-q,1}); ev.push_back({(ld)W.JH[t+512]+1-q,-1}); }
    std::sort(ev.begin(),ev.end()); int cur=0,best=0; size_t i=0; ld bb=0;
    while(i<ev.size()){ ld x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;}
        if(cur>best){best=cur;bb=x;} }
    if(bout)*bout=bb; return best; }
int main(){ build_tables(); if(!load_windows(W))return 1;
    for(int t=-512;t<=511;t++) if(W.ok[t+512]) AT.push_back(t);
    ld lo=39.4690L,hi=39.4760L; int N=700000; int best=0; ld ba=0,bb=0;
    #pragma omp parallel
    { int lb=0; ld la=0;
      #pragma omp for schedule(static)
      for(int i=0;i<=N;i++){ ld a=lo+(hi-lo)*i/N; int s=cover(a,0,0); if(s>lb){lb=s;la=a;} }
      #pragma omp critical
      { if(lb>best){best=lb;ba=la;} } }
    cover(ba,0,0,&bb);
    printf("(1) even  J=floor(a t^2 + b): best %d/1022 at a=%.9Lf (=a*2^17=%.3Lf), b=%.4Lf\n",best,ba,ba*131072,bb);
    // exact feasible a-interval for that coverage
    ld al=ba,ah=ba; { ld st=(hi-lo)/N; while(cover(al-st,0,0)>=best) al-=st; while(cover(ah+st,0,0)>=best) ah+=st; }
    printf("    a range holding that score: [%.9Lf, %.9Lf]  -> a*2^17 in [%.2Lf,%.2Lf]\n",al,ah,al*131072,ah*131072);
    printf("    KC/2^17 = %.9Lf   4*pi^2 = %.9Lf\n",(ld)KC/131072.0L,4*3.14159265358979323846L*3.14159265358979323846L);
    int b3=0; ld a3=0;
    #pragma omp parallel
    { int lb=0; ld la=0;
      #pragma omp for schedule(static)
      for(int i=0;i<=N;i++){ ld a=lo+(hi-lo)*i/N; int s=cover(a,0,3); if(s>lb){lb=s;la=a;} }
      #pragma omp critical
      { if(lb>b3){b3=lb;a3=la;} } }
    printf("(3) even + pure sign offset: best %d/1022 at a=%.9Lf\n",b3,a3);
    // (2)/(4): scan c too
    for(int mode=1;mode<=2;mode++){ int bs=0; ld ba2=0,bc=0;
      #pragma omp parallel
      { int lb=0; ld la=0,lc=0;
        #pragma omp for schedule(dynamic)
        for(int ic=-400;ic<=400;ic++){ ld c=ic*0.0005L;
            for(int i=0;i<=3000;i++){ ld a=lo+(hi-lo)*i/3000; int s=cover(a,c,mode);
                if(s>lb){lb=s;la=a;lc=c;} } }
        #pragma omp critical
        { if(lb>bs){bs=lb;ba2=la;bc=lc;} } }
      printf("(%d) %s: best %d/1022 at a=%.9Lf c=%.5Lf\n",mode==1?2:4,mode==1?"+c|t|":"+c t",bs,ba2,bc); }
    // where does the best even model miss?
    ld b0; cover(ba,0,0,&b0); b0+=1e-9L;
    std::vector<int> miss; for(int t:AT){ ld v=floorl(ba*(ld)t*(ld)t+b0); if(v<W.JL[t+512]||v>W.JH[t+512]) miss.push_back(t); }
    printf("even model misses %zu t:",miss.size()); for(size_t i=0;i<miss.size()&&i<80;i++)printf(" %d",miss[i]); puts("");
    return 0; }
