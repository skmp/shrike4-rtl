// Exact rational feasibility certificate: V(t)=A-floor(C*t^2+B).
// The integer A is absorbed into B. No fitted coefficient range is assumed.
// Restrict |t|>=3, where the putative square term is positive (tz == floor).
#include "fsca_common.h"
#include <algorithm>
struct Sample {int t; i64 x,lo,hi;};
struct Bound {i64 p,q;int a,b;};
static bool less(Bound a,Bound b){return (__int128)a.p*b.q<(__int128)b.p*a.q;}
int main(){Fsca f;if(!load_fsca(f))return 2;
 for(int side=0;side<3;++side){std::vector<Sample>s;
 for(int t=-512;t<=511;++t){if(std::abs(t)<3||(side==0&&t>0)||(side==1&&t<0))continue;
 Win w=decode(f.srz[16384+t],f.srn[16384+t]);s.push_back({t,i64(t)*t,-w.hi,1-w.lo});}
 Bound low={-(1ll<<50),1,0,0},high={1ll<<50,1,0,0};
 for(auto a:s)for(auto b:s)if(a.x>b.x){
 Bound l={a.lo-b.hi,a.x-b.x,a.t,b.t},h={a.hi-b.lo,a.x-b.x,a.t,b.t};
 if(less(low,l))low=l;if(less(h,high))high=h;
 }
 printf("side%d C in (%lld/%lld, %lld/%lld); lower witness t=%d,%d; upper witness t=%d,%d; %s\n",side,
 (long long)low.p,(long long)low.q,(long long)high.p,(long long)high.q,low.a,low.b,high.a,high.b,
 less(low,high)?"nonempty slope interval":"IMPOSSIBLE");
 if(!less(low,high)){
 printf("cross products: %lld >= %lld\n",(long long)(low.p*high.q),(long long)(high.p*low.q));
 for(int t:{low.a,low.b,high.a,high.b}){Win w=decode(f.srz[16384+t],f.srn[16384+t]);printf(" t=%d V=[%lld,%lld]\n",t,(long long)w.lo,(long long)w.hi);}}
 }
}
