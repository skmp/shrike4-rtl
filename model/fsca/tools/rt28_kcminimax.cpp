// rt28_kcminimax.cpp -- J_hw = X'(t) - l(t),  X' = (KC + dk) t^2 / 2^17 (exact, symmetric) or the chain with KC + dk.
// For each dk: the smallest interval [a, a+R) of l that covers EVERY offset (minimax; sensitive to the outliers), the
// interval's position, and the bulk counts for R = 1, 2, 4.   dk in steps of 1/4.
#include "base2.h"
#include <cmath>
#include <vector>
int main(){ build_tables(); FILE*f=fopen("jwin.txt","r"); static long JL[1024],JH[1024]; int tt; long a,b,fl; while(fscanf(f,"%d %ld %ld %ld",&tt,&a,&b,&fl)==4){JL[tt+512]=a;JH[tt+512]=b;}
    for(int mode=0;mode<2;mode++){ printf("\nreference: %s\n   dk     min R covering all   at a      | offsets feasible with R=1   R=2   R=4 (best a)\n",mode?"chain (signed floor first latch) with KC+dk":"exact symmetric (KC+dk) t^2 / 2^17");
        for(int q=-16;q<=40;q++){ long double dk=q/4.0L; std::vector<long double> lo,hi;
            for(int t=-512;t<=511;t++){ if(!t||JL[t+512]>JH[t+512])continue; long double kc=(long double)KC+dk; long double X;
                if(mode==0) X=kc*t*t/131072.0L; else { long double I1=floorl(kc*t/1024.0L+1.0L/16); X=I1*t/128.0L; }
                lo.push_back(X-JH[t+512]); hi.push_back(X-JL[t+512]); }
            long double bestR=1e9,bA=0; for(long double a=-40;a<=20;a+=0.0625L){ long double need=0; bool ok=true; for(size_t i=0;i<lo.size();i++){ if(hi[i]<a){ok=false;break;} long double r=lo[i]-a; if(r>need)need=r; } if(ok&&need<bestR){bestR=need;bA=a;} }
            int bc[3]; int Rs[3]={1,2,4}; for(int k=0;k<3;k++){ int best=0; for(long double a=-12;a<=12;a+=0.0625L){ int c=0; for(size_t i=0;i<lo.size();i++) if(hi[i]>=a&&lo[i]<a+Rs[k])c++; if(c>best)best=c; } bc[k]=best; }
            if(q%2==0||bestR<9) printf(" %+6.2Lf      %6.2Lf          %+6.2Lf     |        %4d            %4d  %4d\n",dk,bestR,bA,bc[0],bc[1],bc[2]); } }
    return 0; }
