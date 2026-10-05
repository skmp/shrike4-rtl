// f2_step.cpp -- max deviation of a UNIT step of the array from the ideal, D = max |arr(v+1,mul)-arr(v,mul)-mul|,
// over both operand roles.  Needed to make the "this offset constrains nothing" skip in f2_k2.cpp rigorous:
// if the window's P-interval is at least |dx|+D wide, consecutive I1 intervals overlap and every K2 works.
#include "base2.h"
#include <algorithm>
int main(){ build_tables(); i64 D0=0,D1=0; i64 bt=0,bg=0,bv=0;
  for(int g=8;g<=14;g++) for(int t=-512;t<=511;t+=1){ if(!t) continue; i64 mul=(i64)t<<g;
    for(int band=0;band<4;band++){ i64 base = band==0?0: band==1? 1ll<<20 : band==2? 1ll<<24 : (i64)(18466836ll<<7)/mul;
      for(i64 d=-400;d<=400;d++){ i64 v=base+d; if(v<=-(1ll<<26)||v>=(1ll<<26))continue;
        i64 s0=arr(v+1,mul)-arr(v,mul)-mul; if(std::abs(s0)>D0){D0=std::abs(s0);bt=t;bg=g;bv=v;}
        i64 s1=arr(mul,v+1)-arr(mul,v)-mul; if(std::abs(s1)>D1) D1=std::abs(s1); } } }
  printf("role0 arr(v,mul): max |step - mul| = %lld  (t=%lld g=%lld v=%lld)\n",(long long)D0,(long long)bt,(long long)bg,(long long)bv);
  printf("role1 arr(mul,v): max |step - mul| = %lld\n",(long long)D1);
  printf("-> use D = %d (2^18) as the rigorous overlap slack\n",1<<18); return 0; }
