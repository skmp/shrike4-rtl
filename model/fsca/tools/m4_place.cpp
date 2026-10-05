// m4_place.cpp -- the complement/bias hypothesis at EVERY placement in the square path, full 1024 scoring.
//  dxa = operand of the first multiply  (arr(KC, dxa))
//  dxb = operand of the second multiply (arr(I1, dxb))   <- may differ from dxa in hardware
//  I1  = feedback, may carry its own complement bias
//  each shift independently 2s-complement (>>) or sign-magnitude (sm)
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static inline i64 shf(i64 v,int u,int m){ return m? sm(v,u):(v>>u); }
struct Cfg{ int qa,qb,r,c,m1,m2; };
static inline i64 chainJ(int t,const Cfg&g){
    i64 base=(i64)t<<13;
    i64 dxa=base+(t<0?g.qa:0), dxb=base+(t<0?g.qb:0);
    i64 I1=shf(arr(KC,dxa)+K,23,g.m1); if(t<0) I1+=g.r;
    return shf(arr(I1,dxb)+K,18,g.m2)-g.c;
}
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    int best=-1; Cfg bc{}; long long n=0;
    std::vector<std::pair<int,Cfg>> top;
    #pragma omp parallel for collapse(2) schedule(dynamic)
    for(int m1=0;m1<2;m1++) for(int m2=0;m2<2;m2++){
      int lb=-1; Cfg lc{};
      for(int qa=-8;qa<=8;qa++) for(int qb=-8;qb<=8;qb++) for(int r=-16;r<=16;r++) for(int c=-24;c<=24;c++){
        Cfg g{qa,qb,r,c,m1,m2}; int sc=0;
        for(int i=0;i<1024;i++) if(have[i]){ i64 j=chainJ(i-512,g); if(j>=JL[i]&&j<=JH[i]) sc++; }
        if(sc>lb){lb=sc;lc=g;}
      }
      #pragma omp critical
      { n++; top.push_back({lb,lc}); if(lb>best){best=lb;bc=lc;} }
    }
    std::sort(top.begin(),top.end(),[](auto&a,auto&b){return a.first>b.first;});
    printf("baseline (all zero, >>,>>) = ");
    { Cfg z{0,0,0,0,0,0}; int sc=0; for(int i=0;i<1024;i++) if(have[i]){i64 j=chainJ(i-512,z); if(j>=JL[i]&&j<=JH[i])sc++;} printf("%d/1024\n\n",sc); }
    printf("best per shift convention:\n");
    for(auto&p:top) printf("  shifts=(%s,%s)  %4d/1024   qa=%+d qb=%+d r=%+d c=%+d\n",
        p.second.m1?"sm":">>",p.second.m2?"sm":">>",p.first,p.second.qa,p.second.qb,p.second.r,p.second.c);
    printf("\nBEST OVERALL %d/1024\n",best);
    // does the best hit the pinned offsets?
    printf("  at best: J(17)=%lld (need %lld)  J(342)=%lld (need %lld)  J(-342)=%lld (need [%lld,%lld])\n",
        (long long)chainJ(17,bc),(long long)JL[512+17],(long long)chainJ(342,bc),(long long)JL[512+342],
        (long long)chainJ(-342,bc),(long long)JL[512-342],(long long)JH[512-342]);
    return 0; }
