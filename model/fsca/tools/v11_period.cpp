// v11_period.cpp -- budget-free test of the node PERIOD, in exact integers.
// If the table had 32 entries (2048-wide nodes) the residual ripple would repeat with period 2048 and
// the profile at even multiples of 1024 would differ from the profile at odd multiples.
// Everything is doubled integers: 2E(a) = (VL[a]+VH[a]) - 2*RN(sin*2^26).
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <climits>
static Fsca F; static i64 VL[65536],VH[65536],S26[65536];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;}
        int q=a>>14,r=a&16383,neg=(q>=2),u=(q&1)?16384-r:r;
        S26[a]=rn_scale(sin_(PI_()*u/32768),26)*(neg?-1:1); }
    auto E2=[&](int a){ return (VL[a]+VH[a]) - 2*S26[a]; };   // 2*E(a), exact integer

    printf("=== node-uniform residual profile  E(1024n+t), averaged over n, split by parity of n ===\n");
    printf("  (2E summed over nodes; printed /2/count.  If nodes were 2048 wide these two columns diverge.)\n");
    printf("     t      n even (n=2..14)   n odd (n=1..15)    difference\n");
    long long gdiff=0; int gc=0;
    for(int t=-512;t<=511;t++){
        long long se=0,so=0; int ce=0,co=0;
        for(int n=1;n<=15;n++){ int a=1024*n+t; if(a<0||a>16384) continue;
            if(n&1){ so+=E2(a); co++; } else { se+=E2(a); ce++; } }
        double ee=(double)se/(2.0*ce), eo=(double)so/(2.0*co);
        gdiff += (long long)(fabs(ee-eo)*1000); gc++;
        if(t%64==0||t==511) printf("  %5d   %14.4f   %14.4f   %11.4f\n",t,ee,eo,ee-eo); }
    printf("  MEAN |even-odd| over all 1024 offsets = %.4f units\n",(double)gdiff/1000.0/gc);

    printf("\n=== the same test the other way round: profile vs offset MOD 2048 ===\n");
    printf("  mean E at a == 0 mod 1024 vs a == 512 mod 1024, and 0 vs 1024 mod 2048\n");
    {   long long s0=0,s512=0; int c0=0,c512=0;
        for(int n=1;n<=15;n++){ s0+=E2(1024*n); c0++; s512+=E2(1024*n+512); c512++; }
        printf("  E at 1024n         : %.4f  (n=1..15)\n",(double)s0/(2.0*c0));
        printf("  E at 1024n+512     : %.4f\n",(double)s512/(2.0*c512));
        long long se=0,so=0; int ce=0,co=0;
        for(int n=1;n<=15;n++){ if(n&1){so+=E2(1024*n);co++;} else {se+=E2(1024*n);ce++;} }
        printf("  E at 2048k (n even): %.4f      E at 2048k+1024 (n odd): %.4f\n",(double)se/(2.0*ce),(double)so/(2.0*co)); }

    printf("\n=== equioscillation signature of the averaged profile (degree-3 minimax has 5 alternations) ===\n");
    {   for(int t : {-512,-400,-362,-342,-300,-200,-100,0,100,200,300,342,362,400,511}){
            long long s=0; int c=0;
            for(int n=1;n<=15;n++){ int a=1024*n+t; if(a<0||a>16384)continue; s+=E2(a); c++; }
            printf("   t=%5d  E=%8.4f\n",t,(double)s/(2.0*c)); } }
    return 0; }
