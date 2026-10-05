// rt26_forced.cpp -- what the coarse windows FORCE about r(t) = J_ref(t) - J_hw(t), for two references:
//   chain : the model's J           exactR : round(KC t^2 / 2^17)   (no chain at all)
// For each threshold c: number of offsets that force r >= c (hardware at least c below) / force r <= -c (above).
// Split by |t| band.  Also: is the forced part EVEN in t?  (offsets where +t and -t force incompatible r)
#include "base2.h"
#include <cmath>
int main(){ build_tables(); FILE*f=fopen("jwin.txt","r"); static long JL[1024],JH[1024]; int tt; long a,b,fl; while(fscanf(f,"%d %ld %ld %ld",&tt,&a,&b,&fl)==4){JL[tt+512]=a;JH[tt+512]=b;}
    for(int ref=0;ref<2;ref++){ printf("\nreference = %s\n",ref?"round(KC t^2/2^17)":"the chain's J");
        puts(" |t| band     n   | forces hw BELOW ref by >=1  >=2  >=3  >=4  >=5 | forces hw ABOVE ref by >=1  >=2  >=3  >=4  >=5 | ref inside window");
        int bands[][2]={{1,63},{64,127},{128,255},{256,383},{384,511}};
        for(auto&B:bands){ int n=0,below[6]={0},above[6]={0},inside=0;
            for(int s=-1;s<=1;s+=2) for(int a0=B[0];a0<=B[1];a0++){ int t=s*a0; if(JL[t+512]>JH[t+512])continue; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 Jc=(arr(I1,dx)+K)>>20; i64 Jr = ref? (i64)llroundl((long double)KC*t*t/131072.0L) : Jc;
                long rlo=Jr-JH[t+512], rhi=Jr-JL[t+512]; n++; if(rlo<=0&&rhi>=0)inside++; for(int c=1;c<=5;c++){ if(rlo>=c)below[c]++; if(rhi<=-c)above[c]++; } }
            printf(" %3d..%-3d  %4d   |                         %4d %4d %4d %4d %4d |                         %4d %4d %4d %4d %4d |   %4d (%.1f%%)\n",B[0],B[1],n,below[1],below[2],below[3],below[4],below[5],above[1],above[2],above[3],above[4],above[5],inside,100.0*inside/n); } }
    // evenness: can one r serve both signs of t?   window of r at +t and at -t relative to the EXACT symmetric reference
    int both=0,incompat=0; long maxgap=0; for(int a0=1;a0<512;a0++){ if(JL[512+a0]>JH[512+a0]||JL[512-a0]>JH[512-a0])continue; both++; long lo=std::max(JL[512+a0],JL[512-a0]), hi=std::min(JH[512+a0],JH[512-a0]); if(lo>hi){incompat++; if(lo-hi>maxgap)maxgap=lo-hi;} }
    printf("\nevenness: of %d offset pairs, %d have DISJOINT coarse windows at +t and -t (largest gap %ld units)\n",both,incompat,maxgap);
    return 0; }
