// Necessary condition for ANY shared square operand (even a lookup table),
// followed by exact coefficient products and separate truncations, no
// coefficient-specific product biases. Uses mirror nodes to cancel odd terms.
#include "fsca_common.h"
#include <algorithm>
struct B{i64 n,d;int t;};
static bool less(B a,B b){return (__int128)a.n*b.d<(__int128)b.n*a.d;}
int main(){Fsca f;if(!load_fsca(f))return 2;
 for(i64 c16:{67108862,67108863})for(int n=1;n<=15;++n){
 Win center=decode(f.srz[n*1024],f.srn[n*1024]),mirror=decode(f.srz[(32-n)*1024],f.srn[(32-n)*1024]);
 for(i64 cn=std::max(center.lo,mirror.lo);cn<=std::min(center.hi,mirror.hi);++cn){B lo{0,1,0},hi{2,1,0};
 for(int t=-512;t<=511;++t){if(std::abs(t)<4)continue;
 Win w=decode(f.srz[16384+t],f.srn[16384+t]),a=decode(f.srz[n*1024+t],f.srn[n*1024+t]),b=decode(f.srz[(32-n)*1024+t],f.srn[(32-n)*1024+t]);
 i64 xl=c16-w.hi,xh=c16-w.lo,yl=cn-(a.hi+b.hi)/2,yh=cn-(a.lo+b.lo+1)/2;
 B l{yl,xh+1,t},h{yh+1,xl,t};if(less(lo,l))lo=l;if(less(h,hi))hi=h;
 }
 printf("C016=%lld node=%d C0=%lld ratio>(%lld/%lld) @t=%d and <(%lld/%lld) @t=%d: %s\n",(long long)c16,n,(long long)cn,(long long)lo.n,(long long)lo.d,lo.t,(long long)hi.n,(long long)hi.d,hi.t,less(lo,hi)?"nonempty":"IMPOSSIBLE");
 printf("cross=%lld vs %lld\n",(long long)(lo.n*hi.d),(long long)(hi.n*lo.d));
 }}
}
