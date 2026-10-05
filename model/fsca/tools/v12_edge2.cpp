// v12_edge2.cpp -- sharp boundary localization with a provable STEP.
// For a 5-point stencil starting at "off" with spacing s (span 4s), a piecewise-cubic function with
// boundaries at B + 1024m has zero 4th difference unless a boundary lies strictly inside the span,
// i.e. off in [B-4s, B-1] (mod 1024).  Average the certificate over s in [64,255] and over all nodes:
//   at r = B-1 EVERY s straddles;  at r = B NO s straddles (it would need s >= 256).
// So P(r) must have its maximum drop exactly at r = B.  Resolution +-1, not +-25.
// Integer arithmetic; /32 for display only.
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
static i64 MD[65536],WD[65536],RW[65536];
static const int C4[5]={1,4,6,4,1};
static void fold(int A0,int A1,int smin,int smax,bool sub,std::vector<double>&P){
    P.assign(1024,0); std::vector<long long> C(1024,0); std::vector<double> S(1024,0);
    for(int s=smin;s<=smax;s++) for(int off=A0; off+4*s<A1; off++){ i64 mid=0,sl=0;
        for(int j=0;j<5;j++){int a=off+j*s; mid+=(j&1?-1:1)*(i64)C4[j]*MD[a]; sl+=(i64)C4[j]*WD[a];}
        i64 v=(mid<0?-mid:mid)-(sub?sl:0); if(v<0)v=0; S[off&1023]+=(double)v/32.0; C[off&1023]++; }
    for(int r=0;r<1024;r++) P[r]= C[r]? S[r]/C[r] : 0; }
static int step_at(const std::vector<double>&P,const char*tag){
    double bd=-1e18; int br=0;
    for(int r=0;r<1024;r++){ double d=P[(r+1023)&1023]-P[r]; if(d>bd){bd=d;br=r;} }
    // runner-up, excluding +-3 of the winner
    double bd2=-1e18; int br2=0;
    for(int r=0;r<1024;r++){ int dd=std::abs(r-br); dd=std::min(dd,1024-dd); if(dd<=3)continue;
        double d=P[(r+1023)&1023]-P[r]; if(d>bd2){bd2=d;br2=r;} }
    printf("   %-28s  max drop %.4f at r=%4d   | runner-up %.4f at r=%4d  (ratio %.2f)\n",tag,bd,br,bd2,br2,bd2>0?bd/bd2:99.0);
    return br; }
static uint32_t hsh(uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;}
static void plant(int q,int nlev,int seed){
    const long double TWOPI=6.283185307179586476925286766559L,w=TWOPI/65536.0L,SC=67108864.0L,H=512.0L;
    for(int a=0;a<65536;a++){ long double A=a; long long k=(long long)std::floor((A-q)/1024.0L);
        long double c=q+1024.0L*k+H, th=w*c, t=A-c;
        long double f0=sinl(th)*SC,f1=w*cosl(th)*SC,f2=-w*w*sinl(th)*SC,f3=-w*w*w*cosl(th)*SC,f4=w*w*w*w*sinl(th)*SC;
        long double qc=f4/24.0L, A0=f0-qc*H*H*H*H/8.0L, A1=f1, A2=f2/2.0L+qc*H*H, A3=f3/6.0L;
        long long iv=(long long)llroundl(A0+A1*t+A2*t*t+A3*t*t*t);
        if(nlev){uint32_t r=hsh((uint32_t)a*2654435761u+seed); if((int)(r%100u)<nlev) iv+=(r&0x10000)?1:-1;}
        MD[a]=2*iv; WD[a]=RW[a]; } }
int main(){ Fsca F; if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ Win wn=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(wn.sign>0){lo=wn.lo;hi=wn.hi;}else{lo=-wn.hi;hi=-wn.lo;} MD[a]=lo+hi; WD[a]=hi-lo; RW[a]=hi-lo; }
    std::vector<i64> RMD(MD,MD+65536), RWD(WD,WD+65536);
    printf("VALIDATION on planted piecewise-cubic data (economised, noise 30%%):\n");
    for(int q : {0,137,256,512,700,900}){ plant(q,30,4242); std::vector<double> P; fold(0,16384,64,255,true,P);
        char t[64]; snprintf(t,64,"plant q=%d (expect %d)",q,q); int r=step_at(P,t); if(r!=q) printf("      ^ MISLOCATED by %+d\n",((r-q+512)&1023)-512); }
    std::memcpy(MD,RMD.data(),sizeof MD); std::memcpy(WD,RWD.data(),sizeof WD);
    printf("\nREAL DUMP:\n");
    { std::vector<double> P; fold(0,16384,64,255,true,P); step_at(P,"lower half [0,16384)");
      std::vector<double> Q; fold(16384,32768,64,255,true,Q); step_at(Q,"upper half [16384,32768)");
      std::vector<double> R; fold(0,65536,64,255,true,R); step_at(R,"full turn [0,65536)");
      std::vector<double> S2; fold(0,16384,128,255,true,S2); step_at(S2,"lower half, s in [128,255]");
      std::vector<double> S3; fold(0,16384,64,255,false,S3); step_at(S3,"lower half, no slack subtraction");
      printf("\n   folded profile P(r), real lower half, s in [64,255], around r=512:\n   r :");
      for(int r=506;r<=518;r++) printf("%8d",r); printf("\n   P :");
      for(int r=506;r<=518;r++) printf("%8.3f",P[r]); printf("\n");
      printf("   and around r=0 (would be the p=0 partition):\n   r :");
      for(int r=1018;r<1024;r++) printf("%8d",r); for(int r=0;r<=6;r++) printf("%8d",r); printf("\n   P :");
      for(int r=1018;r<1024;r++) printf("%8.3f",P[r]); for(int r=0;r<=6;r++) printf("%8.3f",P[r]); printf("\n"); }
    return 0; }
