// v12_phase2.cpp -- controls + verification for the phase-sweep claim.
//  (V) brute-force verification of the sliding-window-max certificate engine
//  (E) NO-STRUCTURE control: exact RN(sin*2^26), globally smooth, real window widths
//  (G) per-window breakdown p=0 vs p=512 (is the gap uniform, or driven by a couple of windows?)
//  (H) resolution of the statistic: planted q, how far is argmin, and how wide is the <=1.05*min band
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
static i64 MD[65536], WD[65536], RW[65536], RM[65536];
static const int C4[5]={1,4,6,4,1};
static void cert_profile(int A0,int A1,int L,std::vector<i64>&out){
    int nS=A1-L-A0+1; out.assign(nS>0?nS:0,0); if(nS<=0)return;
    std::vector<i64> V; std::vector<int> dq;
    for(int s=1;4*s<=L-1;s++){ int nOff=A1-4*s-A0; if(nOff<=0)break; V.assign(nOff,0);
        for(int i=0;i<nOff;i++){ int off=A0+i; i64 mid=0,sl=0;
            for(int j=0;j<5;j++){int a=off+j*s; mid+=(j&1?-1:1)*(i64)C4[j]*MD[a]; sl+=(i64)C4[j]*WD[a];}
            i64 v=(mid<0?-mid:mid)-sl; V[i]=v>0?v:0; }
        int win=L-4*s; dq.clear(); size_t h=0;
        for(int i=0;i<nOff;i++){ while(dq.size()>h&&V[dq.back()]<=V[i])dq.pop_back(); dq.push_back(i);
            int st=i-win+1; if(st>=0){ while(dq[h]<st)h++; if(st<nS&&V[dq[h]]>out[st])out[st]=V[dq[h]]; } } }
}
static i64 cert_naive(int lo,int L){ i64 best=0;
    for(int s=1;4*s<=L-1;s++) for(int off=lo;off+4*s<=lo+L-1;off++){ i64 mid=0,sl=0;
        for(int j=0;j<5;j++){int a=off+j*s; mid+=(j&1?-1:1)*(i64)C4[j]*MD[a]; sl+=(i64)C4[j]*WD[a];}
        i64 v=(mid<0?-mid:mid)-sl; if(v>best)best=v; } return best; }
static double phase_mean(const std::vector<i64>&c,int A0,int p,int L,int hi,int*cntout=nullptr){
    double s=0;int n=0; for(int k=0;;k++){int lo=p+L*k; if(lo+L-1>=hi)break; int i=lo-A0; if(i<0||i>=(int)c.size())break; s+=(double)c[i]/32.0;n++;}
    if(cntout)*cntout=n; return n?s/n:-1; }
int main(){ Fsca F; if(!load_fsca(F)){printf("no dump\n");return 1;}
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} MD[a]=lo+hi; WD[a]=hi-lo; RM[a]=lo+hi; RW[a]=hi-lo; }
    const int L=1024;
    // (V) verify engine
    { std::vector<i64> c; cert_profile(0,16384,L,c); int bad=0;
      for(int lo : {0,1,511,512,513,777,1024,4096,9000,15359}) if(lo+L<=16384){ i64 n=cert_naive(lo,L); if(n!=c[lo]){bad++;printf("   MISMATCH lo=%d fast=%lld naive=%lld\n",lo,(long long)c[lo],(long long)n);} }
      printf("(V) engine check vs brute force on 10 windows: %s\n", bad?"FAILED":"all equal"); }
    // (G) per-window breakdown
    { std::vector<i64> c; cert_profile(0,16384,L,c);
      printf("(G) per-window certificate, angles [0,16384):\n    k   start(p=0)  cert    start(p=512) cert\n");
      int lower=0,tot=0;
      for(int k=0;k<15;k++){ int l0=1024*k, l5=512+1024*k; double a=-1,b=-1;
        if(l0+L-1<16384)a=(double)c[l0]/32.0; if(l5+L-1<16384)b=(double)c[l5]/32.0;
        printf("   %3d   %6d   %6.3f    %6d   %6.3f %s\n",k,l0,a,l5,b,(b>=0&&a>=0)?(b<a?"p512 lower":"p0 lower"):"");
        if(a>=0&&b>=0){tot++; if(b<a)lower++;} }
      printf("   windows where p=512 is lower: %d/%d\n",lower,tot); }
    // (E) NO-STRUCTURE control: exact rounded sine everywhere
    { for(int a=0;a<65536;a++){ int q=a>>14,r=a&16383,neg=(q>=2),u=(q&1)?16384-r:r;
        i64 v=rn_scale(sin_(PI_()*u/32768),26)*(neg?-1:1); MD[a]=2*v; WD[a]=RW[a]; }
      std::vector<i64> c; cert_profile(0,16384,L,c);
      double mn=1e18,mx=0; int pmn=0,pmx=0; double at0=0,at512=0;
      for(int p=0;p<L;p++){ double m=phase_mean(c,0,p,L,16384); if(m<mn){mn=m;pmn=p;} if(m>mx){mx=m;pmx=p;}
        if(p==0)at0=m; if(p==512)at512=m; }
      printf("(E) NO-STRUCTURE control (exact RN(sin*2^26), one global smooth function, real widths):\n");
      printf("    min %.3f at p=%d ; max %.3f at p=%d ; p=0 %.3f ; p=512 %.3f\n",mn,pmn,mx,pmx,at0,at512);
      printf("    -> this is the certificate floor a NON-piecewise function already produces.\n"); }
    // (H) resolution: planted partitions, band width
    { const long double TWOPI=6.283185307179586476925286766559L, w=TWOPI/65536.0L, SC=67108864.0L;
      auto hsh=[](uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;};
      printf("(H) planted-partition resolution (noise +-1 to match the real within-node floor):\n");
      for(int q : {0,128,256,384,512,640,768,896}){
        for(int a=0;a<65536;a++){ long double A=a; long long k=(long long)std::floor((A-q)/1024.0L);
          long double c0=q+1024.0L*k+512.0L, th=w*c0, t=A-c0;
          long double f0=sinl(th)*SC,f1=w*cosl(th)*SC,f2=-w*w*sinl(th)*SC,f3=-w*w*w*cosl(th)*SC;
          long long iv=(long long)llroundl(f0+f1*t+f2*t*t/2.0L+f3*t*t*t/6.0L);
          iv += (int)(hsh((uint32_t)a*2654435761u)%3u)-1; MD[a]=2*iv; WD[a]=RW[a]; }
        std::vector<i64> c; cert_profile(0,16384,L,c);
        double mn=1e18; int pm=0; std::vector<double> mv(L);
        for(int p=0;p<L;p++){ mv[p]=phase_mean(c,0,p,L,16384); if(mv[p]<mn){mn=mv[p];pm=p;} }
        int band=0; for(int p=0;p<L;p++) if(mv[p]<=mn*1.05) band++;
        int d=pm-q; if(d>512)d-=1024; if(d<-512)d+=1024;
        printf("    q=%3d -> argmin p=%4d (err %+4d)  min %.3f  |  phases within 5%% of min: %3d  | at q %.3f  at q+512 %.3f\n",
               q,pm,d,mn,band,mv[q],mv[(q+512)&1023]); } }
    return 0; }
