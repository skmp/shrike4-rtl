// Diagnostic only: minimum signed RAW-product perturbation at each of the
// three even-path stages that satisfies BOTH mirror windows. No model changes.
// Enumerates latch values (never binary searches the nonmonotone recoded port).
#include "base2.h"
#include <algorithm>
#include <climits>
static const i64 c0[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 distance(i64 raw,i64 lo,i64 hi){return raw<lo?lo-raw:raw>hi?hi-raw:0;}
struct Stat{int count=0,up=0,down=0,unreach=0; i64 maxabs=0;};
int main(){Fsca F;if(!load_fsca(F))return 1;build_tables();Stat st[17][3];
puts("node,t,k,T2lo,T2hi,T2,I1,J,rawdelta1,rawdelta2,rawdelta3");
for(int n=1;n<=16;n++)for(int t=-512;t<512;t++){auto&T=TB[n];i64 dx=i64(t)*8192; i64 odd=sm(arr(T.C1f,dx)+K,23-T.e1)-sm(arr(T.c3*32,(arr(i64(t)*t*8,dx)+K)>>23)+K,26);
i64 lo=LLONG_MIN,hi=LLONG_MAX;for(int m:{n,32-n}){int a=(m*1024+t)&65535;auto w=decode(F.srz[a],F.srn[a]);i64 base=c0[n]+(m>16?-odd:odd);lo=std::max(lo,base-w.hi);hi=std::min(hi,base-w.lo);if(n==16)break;}
i64 p1=arr(KC,dx)+K,I=p1>>23,p2=arr(I,dx)+K,J=p2>>20,p3=arr(J,T.S)+K,q=sm(p3,T.u2);if(q>=lo&&q<=hi)continue;
i64 best[3]={LLONG_MAX,LLONG_MAX,distance(p3,lo*(1LL<<T.u2),(hi+1)*(1LL<<T.u2)-1)};
for(int di=-64;di<=64;di++){i64 v=I+di; i64 j=(arr(v,dx)+K)>>20; i64 out=sm(arr(j,T.S)+K,T.u2);if(out>=lo&&out<=hi){i64 d=distance(p1,v*(1LL<<23),(v+1)*(1LL<<23)-1);if(std::abs(d)<std::abs(best[0]))best[0]=d;}}
for(int dj=-256;dj<=256;dj++){i64 j=J+dj;i64 out=sm(arr(j,T.S)+K,T.u2);if(out>=lo&&out<=hi){i64 d=distance(p2,j*(1LL<<20),(j+1)*(1LL<<20)-1);if(std::abs(d)<std::abs(best[1]))best[1]=d;}}
printf("%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld\n",n,t,T.u2-30,(long long)lo,(long long)hi,(long long)q,(long long)I,(long long)J,(long long)best[0],(long long)best[1],(long long)best[2]);
for(int a=0;a<3;a++){auto&s=st[n][a];s.count++;if(best[a]==LLONG_MAX)s.unreach++;else{if(best[a]>0)s.up++;else s.down++;s.maxabs=std::max(s.maxabs,std::abs(best[a]));}}}
for(int n=1;n<=16;n++)for(int a=0;a<3;a++){auto&s=st[n][a];fprintf(stderr,"node %2d k %d stage %d events %2d raw+ %2d raw- %2d unreachable %2d maxabs %lld\n",n,TB[n].u2-30,a+1,s.count,s.up,s.down,s.unreach,(long long)s.maxabs);}
}
