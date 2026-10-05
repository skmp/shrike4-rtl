// v12_phase3.cpp -- faithful plant: piecewise CHEBYSHEV-ECONOMISED cubic (what the hardware actually
// stores), boundaries at a KNOWN phase q, real dump window widths, noise tuned to the real floor.
// If the hypothesis "1024-wide nodes, boundaries at odd multiples of 512" is right, the plant at
// q=512 must reproduce the REAL 1024-phase profile, not just its minimum.
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
static i64 MD[65536],WD[65536],RW[65536],RMD[65536];
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
            int st=i-win+1; if(st>=0){ while(dq[h]<st)h++; if(st<nS&&V[dq[h]]>out[st])out[st]=V[dq[h]]; } } } }
static double pmean(const std::vector<i64>&c,int A0,int p,int L,int hi){ double s=0;int n=0;
    for(int k=0;;k++){int lo=p+L*k; if(lo+L-1>=hi)break; int i=lo-A0; if(i<0||i>=(int)c.size())break; s+=(double)c[i]/32.0;n++;} return n?s/n:-1; }
static uint32_t hsh(uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;}
// economised cubic plant. mode 0 = economised (minimax-like), 1 = plain Taylor
static void plant(int q,int L,int mode,int nlev,int seed){
    const long double TWOPI=6.283185307179586476925286766559L,w=TWOPI/65536.0L,SC=67108864.0L;
    long double H=L/2.0L;
    for(int a=0;a<65536;a++){ long double A=a; long long k=(long long)std::floor((A-q)/(long double)L);
        long double c=q+(long double)L*k+H, th=w*c, t=A-c;
        long double f0=sinl(th)*SC,f1=w*cosl(th)*SC,f2=-w*w*sinl(th)*SC,f3=-w*w*w*cosl(th)*SC,f4=w*w*w*w*sinl(th)*SC;
        long double qc=f4/24.0L;                         // quartic remainder coefficient
        long double A0=f0,A1=f1,A2=f2/2.0L,A3=f3/6.0L;
        if(mode==0){ A2+=qc*H*H; A0-=qc*H*H*H*H/8.0L; }  // t^4 -> H^2 t^2 - H^4/8 (Chebyshev economisation)
        long double v=A0+A1*t+A2*t*t+A3*t*t*t;
        long long iv=(long long)llroundl(v);
        if(nlev){ uint32_t r=hsh((uint32_t)a*2654435761u+seed); if((int)(r%100u)<nlev) iv += (r&0x10000)?1:-1; }
        MD[a]=2*iv; WD[a]=RW[a]; } }
int main(){ Fsca F; if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ Win wn=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(wn.sign>0){lo=wn.lo;hi=wn.hi;}else{lo=-wn.hi;hi=-wn.lo;} MD[a]=lo+hi; WD[a]=hi-lo; RW[a]=hi-lo; RMD[a]=lo+hi; }
    const int L=1024;
    std::vector<i64> creal; cert_profile(0,16384,L,creal);
    std::vector<double> preal(L); for(int p=0;p<L;p++) preal[p]=pmean(creal,0,p,L,16384);
    // real data resolution band, like-for-like with the planted controls
    { double mn=1e18; int pm=0; for(int p=0;p<L;p++) if(preal[p]<mn){mn=preal[p];pm=p;}
      int b5=0,b10=0,b25=0; for(int p=0;p<L;p++){ if(preal[p]<=mn*1.05)b5++; if(preal[p]<=mn*1.10)b10++; if(preal[p]<=mn*1.25)b25++; }
      printf("REAL: argmin p=%d (%.3f); phases within 5%%: %d, 10%%: %d, 25%%: %d  (of 1024)\n",pm,mn,b5,b10,b25);
      printf("      ties at the minimum: "); for(int p=0;p<L;p++) if(preal[p]<=mn+1e-9) printf("%d ",p); printf("\n"); }
    // noise calibration + profile match, economised plant at q=512
    printf("\nECONOMISED-CUBIC PLANT at q=512, noise sweep (pct of angles perturbed by +-1):\n");
    printf("  nlev  cert@512  cert@0   maxdev-vs-real  meandev  argmin\n");
    for(int nlev : {0,10,20,30,40,50,60}){
        plant(512,L,0,nlev,12345); std::vector<i64> c; cert_profile(0,16384,L,c);
        double md=0,sd=0; int pm=0; double mn=1e18;
        for(int p=0;p<L;p++){ double v=pmean(c,0,p,L,16384); double d=std::fabs(v-preal[p]); if(d>md)md=d; sd+=d;
            if(v<mn){mn=v;pm=p;} }
        printf("  %4d   %6.3f   %6.3f   %10.3f   %8.3f   %4d\n",nlev,pmean(c,0,512,L,16384),pmean(c,0,0,L,16384),md,sd/L,pm); }
    // plain Taylor plant for comparison (shows the cross-boundary penalty is coefficient-dependent)
    { plant(512,L,1,30,12345); std::vector<i64> c; cert_profile(0,16384,L,c);
      printf("\nTAYLOR (non-economised) plant at q=512, nlev=30: cert@512 %.3f  cert@0 %.3f  -> real is %.3f/%.3f\n",
        pmean(c,0,512,L,16384),pmean(c,0,0,L,16384),preal[512],preal[0]); }
    // resolution of the faithful plant: can it recover q?
    printf("\nECONOMISED plant, nlev=30, recovery of a planted q:\n");
    for(int q : {0,128,256,384,512,640,768,896}){
        plant(q,L,0,30,999); std::vector<i64> c; cert_profile(0,16384,L,c);
        double mn=1e18; int pm=0; std::vector<double> v(L);
        for(int p=0;p<L;p++){ v[p]=pmean(c,0,p,L,16384); if(v[p]<mn){mn=v[p];pm=p;} }
        int b5=0; for(int p=0;p<L;p++) if(v[p]<=mn*1.05) b5++;
        int d=pm-q; if(d>512)d-=1024; if(d<-512)d+=1024;
        printf("   q=%3d -> argmin %4d (err %+4d) min %.3f  band5%% %3d  | at q %.3f  at q+512 %.3f\n",q,pm,d,mn,b5,v[q],v[(q+512)&1023]); }
    // side-by-side profile, every 32 phases: real vs best-matching plant
    { plant(512,L,0,30,12345); std::vector<i64> c; cert_profile(0,16384,L,c);
      printf("\n   p   real   plant(econ,q=512,n30)\n");
      for(int p=0;p<L;p+=32) printf("  %4d  %5.3f   %5.3f\n",p,preal[p],pmean(c,0,p,L,16384)); }
    return 0; }
