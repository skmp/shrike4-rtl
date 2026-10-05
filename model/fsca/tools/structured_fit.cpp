// Integer-only fit with RN23 theoretical linear coefficients and a candidate
// fixed-grid cubic ROM (round Taylor C3 to 2^-54). No exception table.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <algorithm>
#include <array>
#include <climits>
struct Sample{int t; i64 lo,hi;};
struct Fit{i64 c0,c[3];int sh[3];i64 k[2][3];int misses;};
static std::vector<Sample> samples;
static i64 term(const Fit& f,int k,int t){i64 p=t;if(k>0)p*=t;if(k>1)p*=t;return (std::abs(f.c[k])*p+f.k[t>=0][k])/(1ll<<f.sh[k])*(f.c[k]<0?-1:1);}
static int score(const Fit& f){int m=0;for(auto s:samples){i64 v=f.c0;for(int k=0;k<3;++k)v+=term(f,k,s.t);m+=v<s.lo||v>s.hi;}return m;}
static void optimize(Fit& f,int k,int side){
 i64 D=1ll<<f.sh[k],prior=f.k[side<0?0:side][k];
 i64 caplo=k==0?0:-D/8,caphi=D/8;
 std::vector<std::pair<i64,int>> events;events.reserve(2050);
 events.push_back({caplo,0});events.push_back({caphi+1,0});
 for(auto s:samples){if(side>=0&&(s.t>=0)!=side)continue;
 i64 rest=f.c0;for(int j=0;j<3;++j)if(j!=k)rest+=term(f,j,s.t);
 i64 l=s.lo-rest,h=s.hi-rest;if(f.c[k]<0){i64 v=l;l=-h;h=-v;}
 i64 p=s.t;if(k>0)p*=s.t;if(k>1)p*=s.t;p*=std::abs(f.c[k]);
 i64 a=(l>0?l*D:(l-1)*D+1)-p,b=(h<0?h*D:(h+1)*D-1)-p;
 a=std::max(a,caplo);b=std::min(b,caphi);if(a<=b){events.push_back({a,1});events.push_back({b+1,-1});}}
 std::sort(events.begin(),events.end());int cur=0,best=-1;i64 value=prior,dist=LLONG_MAX;
 for(size_t i=0;i<events.size();){size_t j=i;while(j<events.size()&&events[j].first==events[i].first){cur+=events[j].second;++j;}
 if(j<events.size()&&events[i].first>=caplo&&events[j].first<=caphi+1){
 i64 v=std::clamp(prior,events[i].first,events[j].first-1),d=std::abs(v-prior);
 if(cur>best||(cur==best&&d<dist)){best=cur;value=v;dist=d;}}
 i=j;}
 if(side<0)f.k[0][k]=f.k[1][k]=value;else f.k[side][k]=value;
}
static void solve(Fit& f){for(int it=0;it<8;++it){i64 copy[2][3];for(int s=0;s<2;++s)for(int k=0;k<3;++k)copy[s][k]=f.k[s][k];
 optimize(f,0,-1);for(int s=0;s<2;++s)for(int k=1;k<3;++k)optimize(f,k,s);
 bool changed=false;for(int s=0;s<2;++s)for(int k=0;k<3;++k)changed|=copy[s][k]!=f.k[s][k];if(!changed)break;}
 f.misses=score(f);}
int main(int argc,char**argv){Fsca data;if(!load_fsca(data))return 2;
 unsigned m1[]={0xc90fda,0xc81800,0xc532d6,0xc0677e,0xb9c1ca,0xb15220,0xa72d48,0x9b6c48,0x8e2c18,0xff1acc,0xdf6876,0xbd8f54,0x99e2da,0xe975f6,0x9ce6a4,0x9da8fe,0};
 unsigned m3[]={0xa56,0xa49,0xa23,0x9e4,0x98c,0x91d,0x898,0x7fd,0x74f,0x68f,0x5be,0x4df,0x3f5,0x300,0x204,0x103,0};
 int e1[]={0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3,0};
 int n0=argc>1?atoi(argv[1]):0,n1=argc>2?atoi(argv[2]):16;
 if(n0<0||n1>31||n0>n1){fprintf(stderr,"usage: structured_fit [first_node last_node], 0 <= first <= last <= 31\n");return 2;}
 for(int n=n0;n<=n1;++n){int nn=n<=16?n:32-n;samples.clear();
 for(int t=-512;t<=511;++t){int i=(n*1024+t+65536)%65536;Win w=decode(data.srz[i],data.srn[i]);i64 a=w.sign*w.lo,b=w.sign*w.hi;if(a>b)std::swap(a,b);samples.push_back({t,a,b});}
 Fit initial{};initial.c0=FSCA_TAB[n][0];initial.c[0]=m1[nn]*(n<=16?1ll:-1ll);initial.c[1]=FSCA_TAB[n][2];initial.c[2]=i64(m3[nn])*4096*(n<=16?-1:1);
 initial.sh[0]=11-e1[nn];initial.sh[1]=29;initial.sh[2]=40;
 for(int s=0;s<2;++s){initial.k[s][0]=80;initial.k[s][1]=0;initial.k[s][2]=(1ll<<40)*45/10000;}
 i64 base=std::abs(initial.c[1]);int e2=base?std::max(0,msb(base)-22):0;i64 step=1ll<<e2; // RN23 grid
 initial.c[1]=-((base+step/2)/step)*step;
 Fit best=initial;solve(best);int start=best.misses;
 Win center=decode(data.srz[n*1024],data.srn[n*1024]);
 for(i64 c0=center.lo;c0<=center.hi;++c0)for(i64 c2=(base/step)*step-1024;c2<=(base/step)*step+1024;c2+=step){if(nn==0&&c2!=0)continue;
 Fit f=initial;f.c0=c0;f.c[1]=-c2;solve(f);
 if(f.misses<best.misses||(f.misses==best.misses&&std::abs(c2-base)<std::abs(std::abs(best.c[1])-base)))best=f;}
 printf("node%2d misses %d -> %d C0=%lld C1=%lld/2^%d C2=%lld C3=%lld K1=%lld c2=[%lld,%lld]/2^29 c3=[%lld,%lld]/2^40\n",n,start,best.misses,(long long)best.c0,(long long)best.c[0],best.sh[0],(long long)best.c[1],(long long)best.c[2],(long long)best.k[0][0],(long long)best.k[0][1],(long long)best.k[1][1],(long long)best.k[0][2],(long long)best.k[1][2]);fflush(stdout);
 }
}
