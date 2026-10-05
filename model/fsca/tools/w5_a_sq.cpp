// w5_a_sq.cpp -- BRANCH 5, HYPOTHESIS A: t computed in 2s complement, consumed as 1s complement
// (or sign-magnitude).  Square path only, scored against jwin18.txt (valid because T1/T3 are untouched).
//   value used for t<0 :  t + d        (d = +1 is "2s computed, 1s read" ; d = -1 is the converse)
//   placements: da = first multiply's operand, db = second multiply's operand, r = the I1 feedback latch
//   shift conventions: each latch independently >> (floor) or sm() (one's-complement magnitude)
// Also: the sign-magnitude reading value = -(2^(n-1)+t) for a field of width n.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static inline i64 shf(i64 v,int u,int m){ return m? sm(v,u):(v>>u); }
struct Cfg{ int da,db,r,m1,m2; };
static inline i64 chainJ(int t,const Cfg&g){
    i64 dxa=(i64)(t+(t<0?g.da:0))<<13, dxb=(i64)(t+(t<0?g.db:0))<<13;
    i64 I1=shf(arr(KC,dxa)+K,23,g.m1); if(t<0) I1+=g.r;
    return shf(arr(I1,dxb)+K,18,g.m2); }
static inline int score(const Cfg&g,int&pos,int&neg){ int s=0;pos=0;neg=0;
    for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 j=chainJ(t,g);
        if(j>=JL[i]&&j<=JH[i]){ s++; if(t>=0)pos++; else neg++; } } return s; }
static inline void landmarks(const Cfg&g,const char*tag){
    printf("   %s  J(17)=%lld[need 45617] J(342)=%lld[need 18466836] J(-342)=%lld[need 18466858..67]\n",tag,
        (long long)chainJ(17,g),(long long)chainJ(342,g),(long long)chainJ(-342,g)); }
int main(){ build_tables();
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f);
    Cfg z{0,0,0,0,0}; int p,n; int b0=score(z,p,n);
    printf("baseline d=0            : %d/1024  (pos %d/512, neg %d/512)\n",b0,p,n); landmarks(z,"baseline");
    printf("\n--- A1: t-level offset d on the negative branch, every placement, both shift conventions ---\n");
    int best=-1; Cfg bc{}; long long ncfg=0;
    std::vector<std::pair<int,Cfg>> all;
    for(int m1=0;m1<2;m1++) for(int m2=0;m2<2;m2++)
      for(int da=-4;da<=4;da++) for(int db=-4;db<=4;db++) for(int r=-8;r<=8;r++){
        Cfg g{da,db,r,m1,m2}; ncfg++; int pp,nn2; int s=score(g,pp,nn2);
        all.push_back({s,g}); if(s>best){best=s;bc=g;} }
    printf("configs scanned: %lld\n",ncfg);
    std::sort(all.begin(),all.end(),[](auto&a,auto&b){return a.first>b.first;});
    printf("top 10:\n");
    for(int i=0;i<10;i++){ auto&g=all[i].second;
        printf("  %4d/1024  da=%+d db=%+d r=%+d shifts=(%s,%s)\n",all[i].first,g.da,g.db,g.r,
            g.m1?"sm":">>",g.m2?"sm":">>"); }
    printf("BEST %d/1024 at da=%+d db=%+d r=%+d (%s,%s)\n",best,bc.da,bc.db,bc.r,bc.m1?"sm":">>",bc.m2?"sm":">>");
    landmarks(bc,"best    ");
    // the two named readings
    printf("\n--- the two named readings, plain (da=db=d, r=0, >> >>) ---\n");
    for(int d=-4;d<=4;d++){ Cfg g{d,d,0,0,0}; int pp,nn2; int s=score(g,pp,nn2);
        printf("  d=%+d  %4d/1024  (pos %d, neg %d)%s\n",d,s,pp,nn2,
            d==1?"   <- 2s computed / 1s read":(d==-1?"   <- 1s computed / 2s read":"")); }
    printf("\n--- A2: sign-magnitude reading  value = -(2^(n-1)+t) for t<0, field width n ---\n");
    for(int nb=10;nb<=16;nb++){
        struct SM{int nb;} ; // inline scoring
        int s=0,pp=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 tv = t<0 ? -(( (i64)1<<(nb-1))+t) : t;
            i64 dx=tv<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
            if(j>=JL[i]&&j<=JH[i]){s++; if(t>=0)pp++;} }
        printf("  n=%2d  %4d/1024 (pos %d)\n",nb,s,pp); }
    printf("\n--- rigidity check: is chainJ(t) for t>=0 the same in EVERY A-variant? ---\n");
    { bool same=true;
      for(int m1=0;m1<2&&same;m1++) for(int m2=0;m2<2&&same;m2++) for(int da=-4;da<=4&&same;da++)
        for(int db=-4;db<=4&&same;db++) for(int r=-8;r<=8&&same;r++){ Cfg g{da,db,r,m1,m2};
          if(m1||m2){ /* shift convention can matter for t>=0 too: check */ }
          for(int t=0;t<=511;t++) if(chainJ(t,g)!=chainJ(t,z)){ same=false;
              printf("  DIFFERS at t=%d for da=%d db=%d r=%d (%s,%s)\n",t,da,db,r,m1?"sm":">>",m2?"sm":">>"); break; } }
      if(same) printf("  identical for all 512 non-negative t in every one of the scanned configs.\n"); }
    return 0; }
