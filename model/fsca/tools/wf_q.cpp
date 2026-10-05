// wf_q.cpp -- split the problem.  Build the SYMMETRIC latch dataset (the intersection of the +t and -t
// windows, which is tighter than either) and attack the even part on its own, then look at what asymmetry
// is left over.  Writes wf_jsym.txt:  |t|  SL  SH  ok
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <map>
static i64 JL[1024],JH[1024]; static int OK[1024];
static i64 SL[513],SH[513]; static int SOK[513];
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  int nsym=0,pin=0; std::map<i64,int> wh;
  for(int at=0;at<=511;at++){ int ip=at+512, im=(at?-at:0)+512;
    if(!OK[ip]||!OK[im]){ SOK[at]=0; continue; }
    i64 lo=std::max(JL[ip],JL[im]), hi=std::min(JH[ip],JH[im]);
    if(lo>hi){SOK[at]=0;continue;} SL[at]=lo;SH[at]=hi;SOK[at]=1;nsym++; if(lo==hi)pin++;
    i64 w=hi-lo; wh[w<=8?w:(w<=20?15:(w<=50?30:(w<=120?75:999)))]++; }
  { FILE*g=fopen("wf_jsym.txt","w"); for(int at=0;at<=511;at++) fprintf(g,"%d %lld %lld %d\n",at,(long long)SL[at],(long long)SH[at],SOK[at]); fclose(g); }
  printf("symmetric windows: %d of 512 |t| consistent (45 disjoint + t=512 unpaired), pinned %d\n",nsym,pin);
  printf("width histogram:"); for(auto&p:wh)printf(" %lld:%d",(long long)p.first,p.second); printf("\n");
  // smooth quadratic bound on the even part: J = floor(Q*t^2 + C)
  { int best=-1; long double bQ=0,Q0=(long double)KC/131072.0L;
    for(int qi=-20000;qi<=20000;qi++){ long double Q=Q0+qi*1e-6L;
      std::vector<std::pair<long double,int>> ev;
      for(int at=0;at<=511;at++){ if(!SOK[at])continue; long double base=Q*(long double)at*at;
        ev.push_back({(long double)SL[at]-base,1}); ev.push_back({(long double)SH[at]+1-base,-1}); }
      std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>mx)mx=cur;}
      if(mx>best){best=mx;bQ=Q;} }
    printf("best  J=floor(Q t^2 + C)  on the even part: %d / %d   Q=%.8Lf  (KC/2^17=%.8Lf)\n",best,nsym,bQ,Q0); }
  // single multiply through the array
  { int best=-1; i64 bKq=0,bKr=0; int bg=0,brole=0;
    for(int g=0;g<=8;g++) for(int role=0;role<2;role++) for(i64 dk=-40;dk<=40;dk++) for(int kr=0;kr<10;kr++){
      i64 Kq=KC+dk; int sh=17+g; i64 Kr = kr==0?0:(1ll<<(kr+11)); if(kr&&Kr>=(1ll<<sh))continue; int sc=0;
      for(int at=0;at<=511;at++){ if(!SOK[at])continue; i64 sq=((i64)at*at)<<g;
        i64 J=((role?arr(sq,Kq):arr(Kq,sq))+Kr)>>sh; if(J>=SL[at]&&J<=SH[at])sc++; }
      if(sc>best){best=sc;bKq=Kq;bKr=Kr;bg=g;brole=role;} }
    printf("best  J=(arr(Kq,t^2<<g)+Kr)>>(17+g) on the even part: %d / %d  Kq=%lld Kr=%lld g=%d role=%d\n",best,nsym,(long long)bKq,(long long)bKr,bg,brole); }
  // chain (even part: use |t|)
  { int best=-1; int bs1=0; i64 bKq=0,bK1=0,bK2=0;
    for(int s1=18;s1<=30;s1++){ int s2=43-s1; if(s2<6)continue;
      for(i64 dk=-8;dk<=8;dk++) for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++) for(int r2=0;r2<2;r2++){
        i64 Kq=KC+dk,K1=k1==0?0:k1==1?(1ll<<19):k1==2?(1ll<<(s1-1)):(1ll<<s1);
        i64 K2=k2==0?0:k2==1?(1ll<<19):k2==2?(1ll<<(s2-1)):(1ll<<s2); int sc=0;
        for(int at=0;at<=511;at++){ if(!SOK[at])continue; i64 dx=(i64)at<<13;
          i64 I1=(arr(Kq,dx)+K1)>>s1; if(I1>=(1ll<<27)){sc=0;break;}
          i64 J=((r2?arr(dx,I1):arr(I1,dx))+K2)>>s2; if(J>=SL[at]&&J<=SH[at])sc++; }
        if(sc>best){best=sc;bs1=s1;bKq=Kq;bK1=K1;bK2=K2;} } }
    printf("best  chain on |t| (even part): %d / %d  s1=%d s2=%d Kq=%lld K1=%lld K2=%lld\n",best,nsym,bs1,43-bs1,(long long)bKq,(long long)bK1,(long long)bK2); }
  // print the tight even constraints with the exact fractional part
  puts("\n|t|  SL        SH   w  d=SL-floor(KC t^2/2^17) .. SH-floor   frac");
  for(int at=1;at<=511;at++){ if(!SOK[at]||SH[at]-SL[at]>3)continue; i64 q=KC*(i64)at*at, fl=q>>17;
    printf("%4d %9lld %9lld %2lld   [%3lld,%3lld]   %.4f\n",at,(long long)SL[at],(long long)SH[at],(long long)(SH[at]-SL[at]),
      (long long)(SL[at]-fl),(long long)(SH[at]-fl),(double)(q&131071)/131072.0); }
  return 0; }
