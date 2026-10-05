// Signed FIPR-style squarer, isolated at node 16. Integer-only.
#include "fsca_common.h"
#include <algorithm>
#include <climits>
static i64 raw_square(int t,int sh,bool nz){
 uint32_t x=(uint32_t)(t*(1ll<<sh)) & 0xffffff;
 if(x&0x800000)x|=0x3000000;
 i64 mul=t*(1ll<<sh),rows[13],v=0;
 for(int k=0;k<13;++k){int d=booth_digit(x,k);bool neg=nz?((x>>(2*k+1))&1):d<0;
 rows[k]=(d*mul-neg)*(1ll<<(2*k));
 v+=(rows[k]>>16)*(1ll<<16);
 if(neg&&2*k>=18)v+=1ll<<(2*k);
 }
 v+=(1ll<<16)*(((rows[0]>>15)&1)+((rows[1]>>15)&1)+((rows[2]>>15)&1)>=2);
 v-=(1ll<<16)*(((rows[6]>>16)^(rows[7]>>16)^(rows[8]>>16))&1);
 return v;
}
struct S{int t;i64 low,high,p;};
int main(){Fsca f;if(!load_fsca(f))return 2;constexpr i64 D=1ll<<45;
 for(int sh=10;sh<=14;++sh)for(int nz=0;nz<2;++nz)for(int out=0;out<3;++out){std::vector<S>s;
 for(int t=-512;t<=511;++t){Win w=decode(f.srz[16384+t],f.srn[16384+t]);i64 l=67108863-w.hi,h=67108863-w.lo;
 i64 p=raw_square(t,sh,nz);if(out)p=((p+(out==2?(1ll<<19):0))>>20)*(1ll<<20);
 p>>=2*sh-16;
 s.push_back({t,(l>0?l*D:(l-1)*D+1),(h<0?h*D:(h+1)*D-1),p});}
 i64 gap=LLONG_MAX,bc=0,bl=0,bh=0;int count=0,lt=0,ht=0;
 for(i64 c=165553000;c<=165556000;++c){i64 lo=-D,hi=D;int lti=0,hti=0;
 for(auto a:s){i64 l=a.low-c*a.p,h=a.high-c*a.p;if(l>lo){lo=l;lti=a.t;}if(h<hi){hi=h;hti=a.t;}}
 if(lo<=hi)++count;if(lo-hi<gap){gap=lo-hi;bc=c;bl=lo;bh=hi;lt=lti;ht=hti;}}
 printf("shift%d negzero%d out%d feasible%d bestC%lld bias[%lld,%lld]/2^45 gap%lld bindings%d,%d\n",sh,nz,out,count,(long long)bc,(long long)bl,(long long)bh,(long long)gap,lt,ht);
 }
}
