// we_r.cpp -- write J as  J(t) = floor((KC*t^2 + c(t)) / 2^17)  and print the REQUIRED interval for c(t).
// c is the total accumulated truncation of the square path, in units of 2^-17 of a J LSB.
// Also prints what the current chain's c is, and the required c asymmetry.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static const int GT[8]={0,5,2,7,4,1,6,3};
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    const i64 M=1ll<<17;
    printf("# t   cLO      cHI     cmodel   cLO/2^17 cHI/2^17 cmod/2^17  in?\n");
    int nin=0,nt=0; i64 mincl=1ll<<60,maxch=-(1ll<<60);
    std::vector<i64> CL(1024),CH(1024); std::vector<char> okc(1024,0);
    for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; nt++;
        i64 q=(i64)KC*t*t;
        i64 clo=JLo[t+512]*M-q, chi=(JHi[t+512]+1)*M-1-q;
        i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 e2=arr(I1,dx)-I1*dx;
        i64 cm=(i64)KC*t*t*0; // compute model c exactly: J=floor((I1*dx+e2+K)/2^20) ; want floor((q+c)/2^17)
        cm = (I1*dx+e2+K)*1 ; cm = cm/8 - q;      // (I1*dx+e2+K)/2^3 has denominator 2^17
        // careful: J = (I1*dx+e2+K)>>20 and floor((q+c)/2^17) equal when c = (I1*dx+e2+K)/8 - q, and (I1*dx+e2+K) divisible by 8
        if(((I1*dx+e2+K)&7)!=0) printf("DIVCHK t=%d\n",t);
        CL[t+512]=clo;CH[t+512]=chi;okc[t+512]=1;
        bool ok=(cm>=clo&&cm<=chi); if(ok)nin++;
        mincl=std::min(mincl,clo);maxch=std::max(maxch,chi);
        if(argc>1) printf("%5d %9lld %9lld %9lld  %8.4f %8.4f %8.4f  %s\n",t,(long long)clo,(long long)chi,(long long)cm,
            (double)clo/M,(double)chi/M,(double)cm/M, ok?"":"MISS");
    }
    printf("t with windows: %d ; model c inside: %d ; global c range [%.4f,%.4f]*2^17\n",nt,nin,(double)mincl/M,(double)maxch/M);
    // required asymmetry in c
    puts("\n# forced-D |t|: required c on each side (in 2^-17 J units and as a fraction)");
    puts("# a   c(+a) range              c(-a) range            needed (c(-a)-c(+a))/2^17 min");
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!okc[ip]||!okc[im])continue;
        i64 dlo=JLo[im]-JHi[ip]; if(dlo<=0) continue;
        printf("%4d  [%8.4f,%8.4f]  [%8.4f,%8.4f]   %8.4f\n",a,(double)CL[ip]/M,(double)CH[ip]/M,(double)CL[im]/M,(double)CH[im]/M,
            (double)(CL[im]-CH[ip])/M); }
    return 0; }
