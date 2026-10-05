// v1_rmode_where.cpp -- WHERE is the rounding mode applied?
// If it acts ONLY at the final float conversion, then one internal V explains both captures, so
//     |RN| == |RZ|  or  |RN| == |RZ| + 1ulp     (RZ truncates toward zero; RN rounds to nearest)
// and NEVER |RN| < |RZ|, never a gap > 1 ulp, and the decoded window is never empty.
// Any violation proves the mode acts EARLIER in the datapath, which would invalidate every window.
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <map>
static Fsca F;
int main(){ if(!load_fsca(F)) return 1;
    long long same=0, plus1=0, minus=0, big=0, signdiff=0, empty=0, expdiff=0;
    std::map<long long,int> gap;
    for(int a=0;a<65536;a++){
        uint32_t rn=F.srn[a], rz=F.srz[a];
        if((rn>>31)!=(rz>>31)){ signdiff++; continue; }
        long long mn=(long long)(rn&0x7fffffff), mz=(long long)(rz&0x7fffffff);
        long long d=mn-mz;                       // in ulps of the shared exponent, if exponents match
        if((rn>>23&0xff)!=(rz>>23&0xff)) expdiff++;
        gap[d]++;
        if(d==0) same++; else if(d==1) plus1++; else if(d<0) minus++; else big++;
        Win w=decode(rz,rn);
        long long lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        if(lo>hi) empty++;
    }
    printf("HW RN vs HW RZ over all 65536 angles (magnitude difference in raw-bit units):\n");
    for(auto&p:gap) printf("   diff %+lld : %d\n",p.first,p.second);
    printf("\n  |RN| == |RZ|        : %lld\n",same);
    printf("  |RN| == |RZ| + 1ulp : %lld\n",plus1);
    printf("  |RN| <  |RZ|        : %lld   <-- IMPOSSIBLE under single-V\n",minus);
    printf("  |RN| >  |RZ| + 1ulp : %lld   <-- IMPOSSIBLE under single-V\n",big);
    printf("  sign differs        : %lld\n",signdiff);
    printf("  exponent differs    : %lld  (legitimate: RN can carry into the exponent)\n",expdiff);
    printf("  EMPTY decoded window: %lld   <-- would invalidate every window in this project\n",empty);
    printf("\n  VERDICT: %s\n", (minus||big||empty)?
        "the rounding mode acts EARLIER than the final conversion -- windows are INVALID":
        "every angle is explained by ONE internal V rounded two ways -- the mode acts only at the final conversion");
    return 0; }
