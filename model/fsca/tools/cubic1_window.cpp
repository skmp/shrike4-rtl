// Recompute node-1 cubic coefficient windows with the corrected decoder.
// Other terms fixed explicitly; these are conditional, not ROM bounds.
#include "fsca_common.h"
#include "../archive/fsca_ref.h"
#include <algorithm>
#include <climits>
struct S{i64 p,low,high;int t;};
int main(){Fsca f;if(!load_fsca(f))return 2;constexpr i64 D=1ll<<40;
 std::vector<S>s;for(int t=-512;t<=511;++t){Win w=decode(f.srz[1024+t],f.srn[1024+t]);
 i64 rest=6577819+(i64(0xc81800)*t+80)/2048-(i64(16227180)*t*t)/(1ll<<29);
 i64 l=rest-w.hi,h=rest-w.lo;
 s.push_back({i64(t)*t*t,l>0?l*D:(l-1)*D+1,h<0?h*D:(h+1)*D-1,t});}
 printf("Fixed C0=6577819 C1=0xc81800 K1=80 C2=-16227180 K2=0\n");
 for(int mode=0;mode<3;++mode){i64 minc=LLONG_MAX,maxc=LLONG_MIN;int count=0;
 for(i64 c=0xa49000-1024;c<=0xa49000+1024;++c){i64 lo[2]={-D/8,-D/8},hi[2]={D/8,D/8};
 for(auto a:s){int side=mode==2?(a.t>=0):0;lo[side]=std::max(lo[side],a.low-c*a.p);hi[side]=std::min(hi[side],a.high-c*a.p);}
 if(mode==0){lo[0]=std::max<i64>(lo[0],4947802324ll);hi[0]=std::min<i64>(hi[0],4947802324ll);}
 if(lo[0]<=hi[0]&&lo[1]<=hi[1]){minc=std::min(minc,c);maxc=std::max(maxc,c);++count;}
 if(c==0xa49000)printf("C3=a49000 mode=%d K3-=[%lld,%lld] K3+=[%lld,%lld] feasible=%d\n",mode,(long long)lo[0],(long long)hi[0],(long long)lo[mode==2],(long long)hi[mode==2],lo[0]<=hi[0]&&lo[1]<=hi[1]);
 }
 printf("mode=%d C3 feasible integer range=[%llx,%llx] count=%d (scan a48c00..a49400)\n",mode,(long long)minc,(long long)maxc,count);
 }
 // Independent direct float comparison for the candidate and one common bias.
 i64 K=4870000000ll;int rn=0,rz=0;
 for(int t=-512;t<=511;++t){i64 v=6577819+(i64(0xc81800)*t+80)/2048-(i64(16227180)*t*t)/(1ll<<29)-(i64(0xa49000)*t*t*t+K)/D;rn+=fsca_to_float(v,0)!=f.srn[1024+t];rz+=fsca_to_float(v,1)!=f.srz[1024+t];}
 printf("Candidate common K3=%lld direct float mismatches RN=%d RZ=%d\n",(long long)K,rn,rz);
}
