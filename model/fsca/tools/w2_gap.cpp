// w2_gap.cpp -- WHY the joint scan finds nothing: measure, per alignment, how far apart the stored
// constant that t=17 demands and the stored constant that t=342 demands actually are.
// Scans a window around the t=342 bracket widened by +-3000 ppm (30x the answer), records every KCw
// that hits each landmark (K1=K2=2^19, and again with K1,K2 free), and reports the minimum relative
// separation.  For calibration: the real KC sits 183 ppm below 4*pi^2 (the economisation).
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
static void brk(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
struct R{ int w,s1,s2; long double ppm_fix, ppm_free; i64 kc17,kc342; };
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    std::vector<R> res; long long scanned=0, both_reach=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:scanned,both_reach)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++){
      for(int s2=4;s2<=40;s2++){
        long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
        i64 a,b; brk(342,w,s1,s2,a,b); if(b<a) continue;
        long double mid=0.5L*(a+b); i64 lo=(i64)(mid*(1.0L-3e-3L))-4, hi=(i64)(mid*(1.0L+3e-3L))+4;
        if(lo<a-2) lo=std::min(lo,a); if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; if(hi<b) hi=b;
        if(hi-lo>4000000ll) continue;                     // keep the diagnostic bounded
        scanned+=hi-lo+1;
        i64 dx17=(i64)17<<w, dxp=(i64)342<<w;
        std::vector<i64> h17f,h342f,h17r,h342r;
        for(i64 kc=lo;kc<=hi;kc++){
            i64 i17=(arr(kc,dx17)+K)>>s1, j17=(arr(i17,dx17)+K)>>s2;
            if(j17==JL[529]) h17f.push_back(kc);
            i64 ip=(arr(kc,dxp)+K)>>s1, jp=(arr(ip,dxp)+K)>>s2;
            if(jp>=JL[854]&&jp<=JH[854]) h342f.push_back(kc);
            i64 P17=arr(kc,dx17), Pp=arr(kc,dxp); i64 v17=P17>>s1, vp=Pp>>s1;
            // the +1 latch step is only attainable when the remainder is nonzero (breakpoint < 2^s1)
            bool st17=(P17-(v17<<s1))!=0, stp=(Pp-(vp<<s1))!=0;
            int g17=0,gp=0;
            for(int c=0;c<2;c++){ if(c&&!st17) break; i64 Q=arr(v17+c,dx17); i64 q=Q>>s2;
                bool k2step=(Q-(q<<s2))!=0;
                if(q==JL[529]||(k2step&&q+1==JL[529])) g17=1; }
            for(int c=0;c<2;c++){ if(c&&!stp) break; i64 Q=arr(vp+c,dxp); i64 q=Q>>s2;
                bool k2step=(Q-(q<<s2))!=0;
                if((q>=JL[854]&&q<=JH[854])||(k2step&&q+1>=JL[854]&&q+1<=JH[854])) gp=1; }
            if(g17) h17r.push_back(kc); if(gp) h342r.push_back(kc);
        }
        auto mingap=[&](std::vector<i64>&A,std::vector<i64>&B)->long double{
            if(A.empty()||B.empty()) return -1;
            long double best=1e18L;
            for(i64 x:A){ auto it=std::lower_bound(B.begin(),B.end(),x);
                if(it!=B.end()) best=std::min(best,(long double)(*it-x)/(long double)x*1e6L);
                if(it!=B.begin()) best=std::min(best,(long double)(x-*(it-1))/(long double)x*1e6L); }
            return best; };
        long double gf=mingap(h17f,h342f), gr=mingap(h17r,h342r);
        if(gf>=0||gr>=0){ both_reach++;
          #pragma omp critical
          res.push_back({w,s1,s2,gf,gr,h17f.empty()?-1:h17f[0],h342f.empty()?-1:h342f[0]}); }
      } }
    printf("KCw values scanned: %lld ; triples where both landmarks are individually reachable in the window: %lld\n\n",scanned,both_reach);
    std::vector<R> rf,rr;
    for(auto&r:res){ if(r.ppm_fix>=0) rf.push_back(r); if(r.ppm_free>=0) rr.push_back(r); }
    std::sort(rf.begin(),rf.end(),[](const R&a,const R&b){return a.ppm_fix<b.ppm_fix;});
    std::sort(rr.begin(),rr.end(),[](const R&a,const R&b){return a.ppm_free<b.ppm_free;});
    printf("K1=K2=2^19 : %zu triples reach both; smallest separation between the constant t=17 wants\n"
           "             and the constant t=342 wants:\n",rf.size());
    for(size_t i=0;i<rf.size()&&i<10;i++) printf("   w=%2d s1=%2d s2=%2d : %.2Lf ppm   (KCw near %lld)\n",
        rf[i].w,rf[i].s1,rf[i].s2,rf[i].ppm_fix,(long long)rf[i].kc342);
    printf("\nK1,K2 FREE : %zu triples reach both; smallest separation:\n",rr.size());
    for(size_t i=0;i<rr.size()&&i<10;i++) printf("   w=%2d s1=%2d s2=%2d : %.2Lf ppm   (KCw near %lld)\n",
        rr[i].w,rr[i].s1,rr[i].s2,rr[i].ppm_free,(long long)rr[i].kc342);
    return 0; }
