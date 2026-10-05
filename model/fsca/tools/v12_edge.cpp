// v12_edge.cpp -- a SHARPER, independent localization of the node boundary than the window sweep.
// A 5-point stencil of spacing s starting at angle "off" spans [off, off+4s].  If the data is
// piecewise cubic with boundaries at B + 1024m, the 4th-difference certificate of that stencil is
// elevated exactly when a boundary is strictly inside its span, i.e. off in [B-4s, B-1] (mod 1024).
// Folding the certificate over (off mod 1024) therefore produces a PLATEAU of width exactly 4s whose
// right edge is B-1.  That reads the boundary to +-1, where the window-mean sweep only gives +-25.
// All arithmetic integer; the /32 is display only.
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
static i64 MD[65536],WD[65536],RW[65536];
static const int C4[5]={1,4,6,4,1};
static void fold(int s,int A0,int A1,std::vector<double>&P){
    P.assign(1024,0); std::vector<int> C(1024,0);
    for(int off=A0; off+4*s<A1; off++){ i64 mid=0,sl=0;
        for(int j=0;j<5;j++){int a=off+j*s; mid+=(j&1?-1:1)*(i64)C4[j]*MD[a]; sl+=(i64)C4[j]*WD[a];}
        i64 v=(mid<0?-mid:mid)-sl; if(v<0)v=0; P[off&1023]+=(double)v/32.0; C[off&1023]++; }
    for(int r=0;r<1024;r++) if(C[r]) P[r]/=C[r]; }
// find the plateau: threshold at (min+max)/2, report the longest run above it
static void edges(const std::vector<double>&P,int s,const char*tag){
    double mn=1e18,mx=-1e18; for(double v:P){mn=std::min(mn,v);mx=std::max(mx,v);} double th=(mn+mx)/2;
    int best=0,bs=-1,cur=0,cs=-1;
    for(int i=0;i<2048;i++){ int r=i&1023; if(P[r]>th){ if(cur==0)cs=i; cur++; if(cur>best){best=cur;bs=cs;} } else cur=0; }
    int L=bs&1023, R=(bs+best-1)&1023;
    printf("   %-26s s=%3d (4s=%4d) low %.3f high %.3f | plateau [%d..%d] len %d  ->  B = R+1 = %d ; B = L+4s = %d\n",
        tag,s,4*s,mn,mx,L,R,best,(R+1)&1023,(L+4*s)&1023); }
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
    printf("VALIDATION on planted data (boundary known):\n");
    for(int q : {0,256,512,700}){ plant(q,30,4242);
        for(int s : {32,64}){ std::vector<double> P; fold(s,0,16384,P); char t[64]; snprintf(t,64,"plant q=%d",q); edges(P,s,t);} }
    std::memcpy(MD,RMD.data(),sizeof MD); std::memcpy(WD,RWD.data(),sizeof WD);
    printf("\nREAL DUMP, angles [0,16384) (nodes 0..15):\n");
    for(int s : {8,16,32,64,128}){ std::vector<double> P; fold(s,0,16384,P); edges(P,s,"real lower half"); }
    printf("\nREAL DUMP, angles [16384,32768) (nodes 16..31, independent):\n");
    for(int s : {16,32,64}){ std::vector<double> P; fold(s,16384,32768,P); edges(P,s,"real upper half"); }
    printf("\nREAL DUMP, all angles [0,65536):\n");
    for(int s : {16,32,64}){ std::vector<double> P; fold(s,0,65536,P); edges(P,s,"real full turn"); }
    printf("\nProfile around the transition, s=32 (4s=128), real lower half:\n");
    { std::vector<double> P; fold(32,0,16384,P);
      printf("   r:"); for(int r=376;r<=392;r++) printf(" %d",r); printf("\n   P:");
      for(int r=376;r<=392;r++) printf(" %.2f",P[r]); printf("\n");
      printf("   r:"); for(int r=504;r<=520;r++) printf(" %d",r); printf("\n   P:");
      for(int r=504;r<=520;r++) printf(" %.2f",P[r]); printf("\n"); }
    return 0; }
