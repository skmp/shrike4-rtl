// J constraints from mirror sums only: no assumed linear or cubic terms.
#include "base2.h"
#include <algorithm>
static i64 c0[]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){Fsca f;if(!load_fsca(f))return 2;build_tables();
 for(int du:{0,2}){int impossible=0,hit=0,viol=0;for(int t=-512;t<=511;t++){i64 gl=0,gh=12000000ll<<du;int bindl=0,bindh=0;i64 dx=i64(t)*8192,J=(arr((arr(dx,KC)+K)>>23,dx)+K)>>20;
 for(int n=1;n<=16;n++){Win a=decode(f.srz[n*1024+t],f.srn[n*1024+t]);i64 lo,hi;
 if(n==16){lo=c0[n]-a.hi;hi=c0[n]-a.lo;}else{Win b=decode(f.srz[(32-n)*1024+t],f.srn[(32-n)*1024+t]);lo=c0[n]-(a.hi+b.hi)/2;hi=c0[n]-(a.lo+b.lo+1)/2;}
 i64 pred=sm(arr(J,TB[n].S)+K,TB[n].u2);viol+=pred<lo||pred>hi;
 int u=TB[n].u2+du;i64 l=0,h=(12000000ll<<du)+1;while(l<h){i64 m=(l+h)/2;if(sm(arr(m,TB[n].S)+K,u)>=lo)h=m;else l=m+1;}if(l>gl){gl=l;bindl=n;}
 l=-1;h=12000000ll<<du;while(l<h){i64 m=(l+h+1)/2;if(sm(arr(m,TB[n].S)+K,u)<=hi)l=m;else h=m-1;}if(l<gh){gh=l;bindh=n;}
 }
 impossible+=gl>gh;hit+=(J<<du)>=gl&&(J<<du)<=gh;
 if(t==17||t==-17||t==342||t==-342)printf("du%d t%d J interval[%lld,%lld] bindings %d,%d baseline %lld\n",du,t,(long long)gl,(long long)gh,bindl,bindh,(long long)(J<<du));
 }printf("du%d impossible t=%d baseline common-J hits=%d baseline even-pair violations=%d\n",du,impossible,hit,viol);}
}
