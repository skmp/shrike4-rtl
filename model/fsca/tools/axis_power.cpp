// Test a signed fractional offset in the square operand at the pure-square node.
#include "fsca_common.h"
#include <algorithm>
#include <climits>
struct S {int t; i64 lo,hi;};
int main(){Fsca f;if(!load_fsca(f))return 2;std::vector<S> s;
 constexpr i64 F=1ll<<16,D=1ll<<45;
 for(int t=-512;t<=511;++t){Win w=decode(f.srz[16384+t],f.srn[16384+t]);s.push_back({t,67108863-w.hi,67108863-w.lo});}
 for(int mode=0;mode<4;++mode)for(int bit=8;bit<=16;++bit)for(int sign:{-1,1}){
 i64 bestgap=LLONG_MAX,bc=0,bl=0,bh=0;int feasible=0;int lt=0,ht=0;
 for(i64 c=165553800;c<=165554800;++c){i64 lo=-D,hi=D;int lti=0,hti=0;
 for(auto a:s){i64 t=a.t; i64 correction=(mode==0?t:mode==1?std::abs(t):mode==2?std::min<i64>(t,0):std::max<i64>(t,0));
 i64 p=t*t*F+sign*correction*(1ll<<(16-bit));
 i64 low=(a.lo>0?a.lo*D:(a.lo-1)*D+1)-c*p;
 i64 high=(a.hi<0?a.hi*D:(a.hi+1)*D-1)-c*p;
 if(low>lo){lo=low;lti=t;}if(high<hi){hi=high;hti=t;}
 }
 if(lo<=hi)++feasible;
 if(lo-hi<bestgap){bestgap=lo-hi;bc=c;bl=lo;bh=hi;lt=lti;ht=hti;}
 }
 if(feasible||bit==14)printf("mode%d square=t*t%+d*term/2^%d feasible%d bestC%lld bias[%lld,%lld]/2^45 bindings%d,%d gap%lld\n",mode,sign,bit,feasible,(long long)bc,(long long)bl,(long long)bh,lt,ht,(long long)bestgap);
 }
}
