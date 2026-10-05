// Independent integer witness for the cut-15 survivor; tests held-out nodes.
#include "fsca_common.h"
#include "../archive/fsca_ref.h"
#include <algorithm>
#include <climits>
using I=__int128;
constexpr i64 DEN=1ll<<49;
static i64 square_q20(int t){
 unsigned u=unsigned(t)&511;
 int n5=u&1,n6=((u>>2)&1)&&((u&3)!=3),n7=((u>>4)&1)&&(((u>>2)&3)!=3);
 int n8=((u>>6)&1)&&(((u>>4)&3)!=3),n9=((u>>8)&1)&&(((u>>6)&3)!=3);
 int loss=n5+n6+n7+2*n8+8*n9+(n6^n7);
 return i64(t)*t*(1ll<<20)-i64(loss)*(1ll<<12)-63*t+51*128;
}
struct B{I n,d;int t;};static bool less(B a,B b){return a.n*b.d<b.n*a.d;}
static i64 ceildiv(I a,I b){I q=a/b,r=a%b;return (i64)(q+(r>0));}
static i64 floordiv(I a,I b){I q=a/b,r=a%b;return (i64)(q-(r<0));}
int main(){Fsca f;if(!load_fsca(f))return 2;int rn=0,rz=0,count=0;
 for(int n:{1,2,16,30,31,33,34,48,62,63})for(int t=-512;t<=511;++t){int base=n%32,m=base<=16?base:32-base;int odd=base<=16?1:-1;
 i64 c0=m==1?6577819:m==2?13092290:67108863,c2=m==1?16227180:m==2?32298060:165554336;
 i64 sq=(I(c2)*square_q20(t))/DEN,linear=0,cubic=0;
 if(m==1){linear=(i64(0xc81800)*t+80)/2048;cubic=(i64(0xa49000)*t*t*t+4870000000ll)/(1ll<<40);}
 if(m==2){linear=(i64(0xc532d6)*t+64)/2048;cubic=(i64(0xa23000)*t*t*t+(t<0?4947802324ll:6996099072ll))/(1ll<<40);}
 i64 v=c0+odd*(linear-cubic)-sq;if(n>=32)v=-v;int angle=n*1024+t;
 rn+=fsca_to_float(v,0)!=f.srn[angle];rz+=fsca_to_float(v,1)!=f.srz[angle];++count;
 }
 printf("Shared square witness: %d angles RN=%d RZ=%d mismatches\n",count,rn,rz);
 // Held-out mirror pairs: allow every C0 in its center windows and ANY
 // positive real square coefficient, not just the old table or RN23 grid.
 for(int n=3;n<=15;++n){Win a=decode(f.srz[n*1024],f.srn[n*1024]),b=decode(f.srz[(32-n)*1024],f.srn[(32-n)*1024]);int good=0;
 for(i64 c0=std::max(a.lo,b.lo);c0<=std::min(a.hi,b.hi);++c0){B lower{0,1,0},upper{I(1)<<60,1,0};
 for(int t=-512;t<=511;++t){Win w=decode(f.srz[n*1024+t],f.srn[n*1024+t]),z=decode(f.srz[(32-n)*1024+t],f.srn[(32-n)*1024+t]);
 i64 sl=c0-(w.hi+z.hi)/2,sh=c0-(w.lo+z.lo+1)/2,q=square_q20(t);
 B l{sl>0?I(sl)*DEN:I(sl-1)*DEN+1,q,t},h{sh<0?I(sh)*DEN:I(sh+1)*DEN-1,q,t};
 if(less(lower,l))lower=l;if(less(h,upper))upper=h;}
 bool ok=!less(upper,lower);good+=ok;
 printf("node=%d C0=%lld feasible=%d integer coefficient interval=[%lld,%lld] bindings=%d,%d\n",n,(long long)c0,ok,(long long)ceildiv(lower.n,lower.d),(long long)floordiv(upper.n,upper.d),lower.t,upper.t);
 }printf("node=%d accepted C0 choices=%d\n",n,good);}
 return rn||rz;
}
