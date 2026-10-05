// w1_phase.cpp -- (1) slide the 1024-wide cubic-fit window over every phase; (2) per-node evenness of the
// approximation error about the two candidate table points.  Both model-free, integer.
#include "base2.h"
#include <cstring>
#include <algorithm>
static Fsca F; static i64 VL[65536],VH[65536]; static i64 S26[65536];
int main(){ if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;}
        int q=a>>14,r=a&16383,neg=(q>=2),u=(q&1)?16384-r:r; S26[a]=rn_scale(sin_(PI_()*u/32768),26)*(neg?-1:1); }
    const int C4[5]={1,4,6,4,1};
    // (1) phase sweep: for each phase p, intervals [p+1024k, p+1024k+1023] for k covering [0,16384)
    printf("(1) PHASE SWEEP -- mean cubic slack over 1024-wide intervals starting at angle p + 1024k\n");
    printf("    A real node boundary inside an interval forces a cubic to bridge two polynomials.\n");
    printf("    p     mean slack   worst\n");
    int bestp=-1; double bestm=1e18;
    for(int p=0;p<1024;p+=32){
        double sum=0; int cnt=0; i64 worst=0;
        for(int k=0;k<15;k++){ int lo=p+1024*k; if(lo+1023>=16384) break;
            i64 best=0;
            for(int s=1;s<=255;s++) for(int off=lo; off+4*s<=lo+1023; off++){
                i64 mid=0,sl=0; for(int i=0;i<5;i++){ int a=off+i*s; i64 m=VL[a]+VH[a], w2=VH[a]-VL[a];
                    mid += (i&1?-1:1)*(i64)C4[i]*m; sl += (i64)C4[i]*w2; }
                i64 v=(mid<0?-mid:mid)-sl; if(v>best)best=v; }
            sum+=(double)best/32.0; cnt++; if(best>worst)worst=best; }
        printf("  %4d   %8.3f   %8.3f%s\n",p,sum/cnt,(double)worst/32.0, (sum/cnt<bestm?"  <-- best so far":""));
        if(sum/cnt<bestm){bestm=sum/cnt;bestp=p;} }
    printf("  best phase p=%d (interval [p+1024k, p+1024k+1023]); node CENTRE = p+512 mod 1024\n\n",bestp);
    // (2) per-node evenness of E about the two candidate table points
    printf("(2) PER-NODE EVENNESS of E(a)=V_hw-RN(sin*2^26): mean |E(c+t)-E(c-t)| over t=1..511\n");
    printf("    c = 1024n (multiples of 1024)   vs   c = 1024n+512\n");
    printf("    node  about 1024n   about 1024n+512\n");
    double g0=0,g1=0; int gc=0;
    for(int n=1;n<=15;n++){
        double s0=0,s1=0;
        for(int t=1;t<=511;t++){
            int c0=1024*n, c1=1024*n+512;
            double e0a=0.5*(VL[c0+t]+VH[c0+t])-S26[c0+t], e0b=0.5*(VL[c0-t]+VH[c0-t])-S26[c0-t];
            double e1a=0.5*(VL[c1+t]+VH[c1+t])-S26[c1+t], e1b=0.5*(VL[c1-t]+VH[c1-t])-S26[c1-t];
            s0+=std::fabs(e0a-e0b); s1+=std::fabs(e1a-e1b); }
        printf("    %3d   %9.3f     %9.3f\n",n,s0/511,s1/511); g0+=s0/511; g1+=s1/511; gc++; }
    printf("    MEAN  %9.3f     %9.3f\n",g0/gc,g1/gc);
    return 0; }
