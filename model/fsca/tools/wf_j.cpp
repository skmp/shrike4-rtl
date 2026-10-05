// wf_j.cpp -- single-multiply square rules:  J = (arr(Kq, t^2 << g) + Kr) >> (17+g)  and role swap.
// The exact t^2 latch is a proven hardware quantity ((arr(dx,dx)+2^19)>>26 == t*t for all 1024 t), so this
// family costs one product instead of two.  It is symmetric in t by construction; the point is to see how
// much of the required deviation d(t) = J - floor(KC t^2/2^17) the array's own row-truncation loss explains.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OK[1024];
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  // symmetric upper bound: how many t can a rule with J(t)=J(-t) satisfy at all?
  { int cap=0; for(int at=0;at<=511;at++){ int ip=at+512, im=(at?-at:0)+512;
      if(at==0){ cap += OK[512]?1:0; continue; }
      bool po=OK[ip],mo=OK[im]; i64 lo=std::max(JL[ip],JL[im]), hi=std::min(JH[ip],JH[im]);
      if(po&&mo){ cap += (lo<=hi)?2:1; } else cap += (po||mo)?1:0; }
    // t=-512 has no mirror
    if(OK[0]) cap+=1;
    printf("upper bound for ANY rule with J(t)=J(-t): %d of 1022\n",cap); }
  printf("\nrule                                    best score  Kq        Kr        role  g\n");
  int gbest=0;
  for(int g=0;g<=10;g++){
    int best=-1; i64 bKq=0,bKr=0; int brole=0;
    for(int role=0;role<2;role++) for(i64 dk=-24;dk<=24;dk++) for(int kr=0;kr<8;kr++){
      i64 Kq=KC+dk; int sh=17+g; i64 Kr = kr==0?0 : (1ll<<(kr+13));
      if(Kr>=(1ll<<sh)&&kr) continue;
      int sc=0;
      for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; i64 sq=((i64)tt*tt)<<g;
        i64 P = role? arr(sq,Kq) : arr(Kq,sq); i64 J=(P+Kr)>>sh;
        if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
      if(sc>best){best=sc;bKq=Kq;bKr=Kr;brole=role;} }
    printf("J=(arr(Kq,t^2<<%2d)+Kr)>>%2d               %5d  %8lld  %8lld  %4d %2d\n",g,17+g,best,(long long)bKq,(long long)bKr,brole,g);
    if(best>gbest)gbest=best; }
  // best rule detail + residual
  { int best=-1; i64 bKq=0,bKr=0; int brole=0,bg=0;
    for(int g=0;g<=10;g++) for(int role=0;role<2;role++) for(i64 dk=-24;dk<=24;dk++) for(int kr=0;kr<8;kr++){
      i64 Kq=KC+dk; int sh=17+g; i64 Kr = kr==0?0 : (1ll<<(kr+13)); if(Kr>=(1ll<<sh)&&kr) continue;
      int sc=0; for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; i64 sq=((i64)tt*tt)<<g;
        i64 P = role? arr(sq,Kq) : arr(Kq,sq); i64 J=(P+Kr)>>sh; if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
      if(sc>best){best=sc;bKq=Kq;bKr=Kr;brole=role;bg=g;} }
    printf("\nBEST single-multiply rule: score %d  Kq=%lld Kr=%lld role=%d g=%d\n",best,(long long)bKq,(long long)bKr,brole,bg);
    int miss=0; printf("first 40 misses:  t   JL   JH   Jmodel  delta\n");
    for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; i64 sq=((i64)tt*tt)<<bg;
      i64 P = brole? arr(sq,bKq) : arr(bKq,sq); i64 J=(P+bKr)>>(17+bg);
      if(J<JL[tt+512]||J>JH[tt+512]){ if(miss<40) printf("  %5d %9lld %9lld %9lld  %+lld\n",tt,(long long)JL[tt+512],(long long)JH[tt+512],(long long)J,
        (long long)(J<JL[tt+512]?J-JL[tt+512]:J-JH[tt+512])); miss++; } }
    printf("total misses: %d\n",miss); }
  return 0; }
