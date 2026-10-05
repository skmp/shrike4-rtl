// wf_e.cpp -- the allowed deviation d(t) = J(t) - floor(KC*t^2/2^17), printed as a compact map,
// plus the exact fractional part of KC*t^2/2^17.  Pure integer arithmetic.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <map>
static i64 JL[1024],JH[1024]; static int OK[1024];
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  std::map<i64,int> hl,hh; i64 gminlo=1000,gmaxhi=-1000;
  printf("   t   fl(KCt^2/2^17)  frac   dlo  dhi   | mirrored t   dlo dhi\n");
  for(int at=0;at<=511;at++){
    for(int sgn=0;sgn<2;sgn++){ int tt = sgn? -at : at; if(at==0&&sgn)continue; if(tt<-512)continue;
      if(!OK[tt+512])continue; i64 q=KC*(i64)tt*tt, fl=q>>17, fr=q&131071;
      i64 dlo=JL[tt+512]-fl, dhi=JH[tt+512]-fl; hl[dlo]++; hh[dhi]++;
      if(dlo<gminlo)gminlo=dlo; if(dhi>gmaxhi)gmaxhi=dhi; }
  }
  printf("dlo histogram:"); for(auto&p:hl)printf(" %lld:%d",(long long)p.first,p.second); printf("\n");
  printf("dhi histogram:"); for(auto&p:hh)printf(" %lld:%d",(long long)p.first,p.second); printf("\n");
  // compact per-|t| table with both sides, only where at least one side is tight (width<=6)
  puts("\n|t|  frac(+)  d(+t) range   frac(-)  d(-t) range   D=J(-t)-J(t) range");
  for(int at=1;at<=511;at++){ int ip=at+512, im=-at+512;
    if(!OK[ip]||!OK[im])continue;
    i64 q=KC*(i64)at*at, fl=q>>17; double fr=(double)(q&131071)/131072.0;
    i64 dlp=JL[ip]-fl,dhp=JH[ip]-fl, dlm=JL[im]-fl,dhm=JH[im]-fl;
    if(dhp-dlp>6 && dhm-dlm>6) continue;
    printf("%4d  %.4f  [%3lld,%3lld]   %.4f  [%3lld,%3lld]   [%3lld,%3lld]\n",at,fr,(long long)dlp,(long long)dhp,fr,
      (long long)dlm,(long long)dhm,(long long)(dlm-dhp),(long long)(dhm-dlp)); }
  return 0; }
