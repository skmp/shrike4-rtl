// f2_arrprobe.cpp -- prerequisites for any "no integer I1 exists" claim.
//  (a) Is arr(v, mul) monotone in the Booth-recoded operand v?  e6_nol1 binary-searched, which ASSUMES it.
//  (b) What is the exact error E(v) = arr(v,mul) - v*mul, and is it bounded?  A bound makes the
//      v-bracket for an exhaustive scan provable rather than heuristic.
#include "base2.h"
#include <algorithm>
#include <cstdio>
int main(){ build_tables();
  long long emin=1e18, emax=-1e18; long nonmono=0; long long worstback=0;
  int wt=0,wg=0; long long wv=0;
  // the multiplicands that can occur: mul = t<<g, |t|<=512, g in 8..14  -> |mul| <= 2^23
  for(int g=8; g<=14; g++){
    for(int tt=-512; tt<=511; tt++){
      if(!tt) continue; i64 mul=(i64)tt<<g;
      // sample v densely in a band around the physically relevant magnitudes, and at random far out
      i64 centre = mul? (18466836ll<<3)/ (mul?mul:1) : 0; if(centre<0) centre=-centre;
      for(int band=0; band<3; band++){
        i64 base = band==0?0 : (band==1? centre : (1ll<<24));
        i64 prev=0; bool have=false;
        for(i64 d=-3000; d<=3000; d++){
          i64 v=base+d; if(v<=-(1ll<<26)||v>=(1ll<<26)) continue;
          i64 a=arr(v,mul); i64 e=a - v*mul;
          if(e<emin)emin=e; if(e>emax)emax=e;
          if(have){ i64 step=a-prev; i64 want = mul>0?1:-1;
            if((mul>0 && step<0)||(mul<0 && step>0)){ nonmono++; i64 b=(mul>0)?-step:step; if(b>worstback){worstback=b;wt=tt;wg=g;wv=v;} } }
          prev=a; have=true; }
      }
    }
  }
  printf("E(v)=arr(v,mul)-v*mul over the sampled band: min %lld  max %lld\n",(long long)emin,(long long)emax);
  printf("non-monotone steps: %ld   worst backward step %lld (t=%d g=%d v=%lld)\n",nonmono,(long long)worstback,wt,wg,wv);
  // theoretical bound check: 14 row truncations of <2^16 each + lost hot ones (k=0..8) + m15/p16 terms
  long long bound = 14ll*65536 + ((1ll<<18)-1)/3 + 65536 + 65536;
  printf("conservative bound |E| < %lld ; measured |E| <= %lld\n",bound,std::max(-emin,emax));
  return 0; }
