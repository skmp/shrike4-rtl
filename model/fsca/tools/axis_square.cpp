// Isolate the square at node 16. All decision arithmetic is integer.
#include "fsca_common.h"
#include <algorithm>
#include <climits>
struct Range { i64 lo=LLONG_MIN/4,hi=LLONG_MAX/4; int lt=0,ht=0; };
int main(){Fsca f;if(!load_fsca(f))return 2; constexpr i64 D=1ll<<29;
 int asym=0;for(int t=1;t<=511;++t){int p=16384+t,m=16384-t;if(f.srn[p]!=f.srn[m]||f.srz[p]!=f.srz[m]){++asym;Win a=decode(f.srz[p],f.srn[p]),b=decode(f.srz[m],f.srn[m]);printf("t=%d plus=[%lld,%lld] minus=[%lld,%lld]\n",t,(long long)a.lo,(long long)a.hi,(long long)b.lo,(long long)b.hi);}}
 printf("node16 paired RN/RZ asymmetries: %d/511\n",asym);
 for(i64 c0=67108862;c0<=67108863;++c0){int count[3]={};i64 cl[3]={},ch[3]={};Range best[3];i64 gaps[3]={LLONG_MAX,LLONG_MAX,LLONG_MAX};i64 bc[3]={};
 for(i64 c=165553000;c<=165556000;++c){Range r[3];
 for(int t=-512;t<=511;++t){Win w=decode(f.srz[16384+t],f.srn[16384+t]);i64 l=c0-w.hi,h=c0-w.lo;
 // tz(x) in [l,h], numerator integer. Signed zero needs (-1,1).
 i64 low=(l>0?l*D:(l-1)*D+1)-c*t*t;
 i64 high=(h<0?h*D:(h+1)*D-1)-c*t*t;
 for(int k:{t<0?0:1,2}){if(low>r[k].lo){r[k].lo=low;r[k].lt=t;}if(high<r[k].hi){r[k].hi=high;r[k].ht=t;}}
 }
 for(int k=0;k<3;++k){i64 gap=r[k].lo-r[k].hi;
 if(gap<gaps[k]){gaps[k]=gap;best[k]=r[k];bc[k]=c;}
 if(gap<=0){if(!count[k])cl[k]=c;ch[k]=c;++count[k];}}
 }
 for(int k=0;k<3;++k)printf("C0=%lld side=%d feasible C2=[%lld,%lld] (%d); best C2=%lld bias numerator [%lld,%lld]/2^29 bindings %d,%d gap=%lld\n",(long long)c0,k,(long long)cl[k],(long long)ch[k],count[k],(long long)bc[k],(long long)best[k].lo,(long long)best[k].hi,best[k].lt,best[k].ht,(long long)gaps[k]);
 }
}
