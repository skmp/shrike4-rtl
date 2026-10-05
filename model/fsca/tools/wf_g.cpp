// wf_g.cpp -- EXACT inversion to the FIRST latch of the square chain, then a 2-variable feasibility test
// asking whether that latch can be ANY floored affine function of t.
// If no line fits the allowed I1 intervals, the whole "constant x dx -> floor -> x dx" family is dead
// independently of which constants are chosen.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
static i64 JL[1024],JH[1024]; static int OK[1024];
static int S2,ROLE; static i64 K2;
static inline i64 Jof(i64 I1,i64 dx){ i64 P = ROLE? arr(dx,I1) : arr(I1,dx); return (P+K2)>>S2; }
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  printf(" s2  K2            role  nonempty  mingap      a*          viol@best\n");
  for(S2=14;S2<=25;S2++) for(int k2=0;k2<5;k2++) for(ROLE=0;ROLE<2;ROLE++){
    K2 = k2==0?0 : k2==1?(1ll<<19) : k2==2?(1ll<<(S2-1)) : k2==3?(1ll<<S2) : (1ll<<(S2-2));
    std::vector<i64> IL(1024),IH(1024); std::vector<char> ok2(1024,0); int nonempty=0;
    for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512]||tt==0) continue; i64 dx=(i64)tt<<13;
      long double c=(long double)KC*tt*powl(2.0L,(long double)S2-30.0L); i64 c0=(i64)llroundl(c);
      i64 W=40000; i64 lo,hi;
      if(tt>0){ // J non-decreasing in I1
        i64 a1=c0-W,b1=c0+W+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(Jof(m,dx)>=JL[tt+512])b1=m;else a1=m+1;} lo=a1;
        i64 a2=c0-W-1,b2=c0+W; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(Jof(m,dx)<=JH[tt+512])a2=m;else b2=m-1;} hi=a2;
      } else {  // J non-increasing in I1 (I1 negative, dx negative)
        i64 a1=c0-W,b1=c0+W+1; while(a1<b1){i64 m=a1+(b1-a1)/2; if(Jof(m,dx)<=JH[tt+512])b1=m;else a1=m+1;} lo=a1;
        i64 a2=c0-W-1,b2=c0+W; while(a2<b2){i64 m=a2+(b2-a2+1)/2; if(Jof(m,dx)>=JL[tt+512])a2=m;else b2=m-1;} hi=a2; }
      // verify: every integer in [lo,hi] really satisfies the window, and lo-1 / hi+1 do not
      if(lo<=hi){ int bad=0; for(i64 v=lo;v<=hi && v<=lo+2048;v++){ i64 J=Jof(v,dx); if(J<JL[tt+512]||J>JH[tt+512]){bad=1;break;} }
        i64 Jm=Jof(lo-1,dx), Jp=Jof(hi+1,dx);
        if(Jm>=JL[tt+512]&&Jm<=JH[tt+512]) bad=1; if(Jp>=JL[tt+512]&&Jp<=JH[tt+512]) bad=1;
        if(!bad){ IL[tt+512]=lo; IH[tt+512]=hi; ok2[tt+512]=1; nonempty++; } } }
    // convex feasibility: minimise  f(a) = max_t (IL(t)-a t)  -  min_t (IH(t)+1-a t)
    auto fval=[&](long double aa, long double* bstar)->long double{
      long double L=-1e30L,U=1e30L; for(int tt=-512;tt<=511;tt++){ if(!ok2[tt+512])continue;
        long double l=IL[tt+512]-aa*tt, u=IH[tt+512]+1-aa*tt; if(l>L)L=l; if(u<U)U=u; }
      if(bstar)*bstar=L; return L-U; };
    long double alo=(long double)KC*powl(2.0L,(long double)S2-30.0L)-2.0L, ahi=alo+4.0L;
    for(int it=0;it<300;it++){ long double m1=alo+(ahi-alo)/3, m2=ahi-(ahi-alo)/3;
      if(fval(m1,nullptr)<fval(m2,nullptr)) ahi=m2; else alo=m1; }
    long double astar=(alo+ahi)/2, bs; long double gap=fval(astar,&bs);
    // at a*, how many t can one b satisfy?
    std::vector<std::pair<long double,int>> ev;
    for(int tt=-512;tt<=511;tt++){ if(!ok2[tt+512])continue; ev.push_back({IL[tt+512]-astar*tt,1}); ev.push_back({IH[tt+512]+1-astar*tt,-1}); }
    std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>mx)mx=cur;}
    printf("%3d  %-12lld  %d   %4d     %9.3Lf  %13.6Lf  %4d\n",S2,(long long)K2,ROLE,nonempty,gap,astar,nonempty-mx); }
  return 0; }
