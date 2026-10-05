// v12_edge3.cpp -- EXACT-COMBINATORICS boundary localization.
// With spacing s=255 a stencil starting at "off" covers the 1021 consecutive angles [off, off+1020].
// Boundaries sit 1024 apart, so exactly FOUR residues r = off mod 1024 contain no boundary strictly
// inside the span: r in {B, B+1, B+2, B+3}.  For piecewise-cubic data the 4th difference is then
// identically zero (up to truncation noise) at exactly those four residues and nonzero elsewhere.
// So: score(B) = mean P over {B..B+3} - mean P elsewhere, minimised at the true B.  Localization +-0.
// Validated on planted data before it is applied to the dump.  Integer arithmetic; /32 display only.
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
static i64 MD[65536],WD[65536],RW[65536];
static const int C4[5]={1,4,6,4,1};
static void foldS(int A0,int A1,int s,bool sub,std::vector<double>&P,std::vector<long long>&C){
    P.assign(1024,0); C.assign(1024,0);
    for(int off=A0; off+4*s<A1; off++){ i64 mid=0,sl=0;
        for(int j=0;j<5;j++){int a=off+j*s; mid+=(j&1?-1:1)*(i64)C4[j]*MD[a]; sl+=(i64)C4[j]*WD[a];}
        i64 v=(mid<0?-mid:mid)-(sub?sl:0); if(v<0)v=0; P[off&1023]+=(double)v/32.0; C[off&1023]++; }
    for(int r=0;r<1024;r++) if(C[r]) P[r]/=C[r]; }
static int locate(const std::vector<double>&P,const char*tag,int expect){
    double best=1e18,second=1e18; int bb=0;
    std::vector<double> sc(1024);
    double tot=0; for(double v:P) tot+=v;
    for(int B=0;B<1024;B++){ double in=0; for(int j=0;j<4;j++) in+=P[(B+j)&1023];
        sc[B]= in/4.0 - (tot-in)/1020.0; if(sc[B]<best){best=sc[B];bb=B;} }
    for(int B=0;B<1024;B++){ int d=std::abs(B-bb); d=std::min(d,1024-d); if(d<=4)continue; if(sc[B]<second)second=sc[B]; }
    printf("   %-30s argmin B=%4d  score %.4f | best non-adjacent %.4f (margin x%.2f)%s\n",
        tag,bb,best,second, second<0? best/second : 0.0,
        expect>=0 ? (bb==expect?"  RECOVERED":"  *** WRONG ***") : "");
    return bb; }
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
    std::vector<double> P; std::vector<long long> C;
    printf("VALIDATION on planted piecewise-cubic data, s=255, full turn:\n");
    for(int q : {0,137,256,433,512,700,900,1000}){ plant(q,30,4242); foldS(0,65536,255,true,P,C);
        char t[64]; snprintf(t,64,"plant q=%d",q); locate(P,t,q); }
    printf("\nREAL DUMP, s=255:\n");
    std::memcpy(MD,RMD.data(),sizeof MD); std::memcpy(WD,RWD.data(),sizeof WD);
    foldS(0,65536,255,true,P,C); int b1=locate(P,"full turn [0,65536)",-1);
    foldS(0,16384,255,true,P,C); int b2=locate(P,"lower half [0,16384)",-1);
    foldS(16384,32768,255,true,P,C); int b3=locate(P,"upper half [16384,32768)",-1);
    foldS(0,65536,255,false,P,C); int b4=locate(P,"full turn, no slack subtr.",-1);
    foldS(0,65536,254,true,P,C); locate(P,"full turn, s=254 (dip 8 wide)",-1);
    printf("\n   REAL folded profile, s=255, full turn, r = 505..525:\n   r :");
    foldS(0,65536,255,true,P,C);
    for(int r=505;r<=525;r++) printf("%7d",r); printf("\n   P :");
    for(int r=505;r<=525;r++) printf("%7.3f",P[r]); printf("\n   n per residue = %lld\n",C[512]);
    printf("\n   consensus: %d %d %d %d\n",b1,b2,b3,b4);
    return 0; }
