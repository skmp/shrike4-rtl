// DIAGNOSTIC constant sweep around complementary C1 reuse; no per-word fitting.
// Reuse C1F[16-n]; sweep one global KC, independent dx placements and port roles.
#include "base2.h"
#include <algorithm>
#include <array>
#include <omp.h>
static const i64 c0[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell{i64 base,lo,hi;}; static Cell cells[32][1024];
struct Cfg{int f,k,d,shape,r,p,d2,dk; int v=0,g=0; std::array<int,17> pn{};};
static i64 sf[28][17];
static i64 scale(i64 x,int s){return s>=0?x*(1LL<<s):(x+(1LL<<(-s-1)))>>(-s);}
int main(int argc,char**){bool linear=true;Fsca F;if(!load_fsca(F))return 1;build_tables();
for(int f=1;f<=27;f++)for(int n=0;n<=16;n++)sf[f][n]=rn_scale(linear?sin_(PI_()*n/32)*PI_()/QONE/2:sin_(PI_()*n/32),f);
for(int m=0;m<32;m++){int n=m>16?32-m:m;for(int t=-512;t<512;t++){auto w=decode(F.srz[(m*1024+t)&65535],F.srn[(m*1024+t)&65535]);i64 dx=i64(t)*8192; i64 odd=sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1)-sm(arr(TB[n].c3*32,(arr(i64(t)*t*8,dx)+K)>>23)+K,26);cells[m][t+512]={c0[n]+(m>16?-odd:odd),w.sign>0?w.lo:-w.hi,w.sign>0?w.hi:-w.lo};}}
i64 kval=linear?(i64)(((i128)KC*2*QONE+PI_()/2)/PI_()):KC; printf("field %s KCbase %lld\n",linear?"complementary linear":"sine",(long long)kval); std::vector<Cfg> cs;for(int p=0;p<=3;p++)for(int k=-4;k<=5;k++)for(int d=10;d<=16;d++)for(int d2=10;d2<=16;d2++)for(int r=0;r<8;r++)for(int dk=-8;dk<=8;dk++)cs.push_back({16,k,d,4,r,p,d2,dk});
printf("configs %zu; f16=normalised S, others fixed fraction bits; all latches >>23\n",cs.size());fflush(stdout);
#pragma omp parallel for schedule(dynamic,16)
for(size_t j=0;j<cs.size();j++){auto &c=cs[j];bool bad=false;auto mul=[&](i64 a,i64 b,int bit){if(c.r&(1<<bit))std::swap(a,b);if(a<-(1LL<<27)||a>=(1LL<<27)||b<-(1LL<<24)||b>=(1LL<<24)){bad=true;return i64(0);}return arr(a,b)+K;};
for(int n=1;n<=16&&!bad;n++){int fs=c.f==16?(linear?22-TB[16-n].e1:TB[n].u2-30+23):c.f; i64 S=c.f==16?(linear?TB[16-n].C1f:TB[n].S):sf[c.f][n];i64 kc=scale(kval,c.k)+c.dk;S*=1LL<<c.p; int shift=18+c.k+fs+c.p+c.d+c.d2+6-46;if(shift<20||shift>60){bad=true;break;}
for(int t=-512;t<512&&!bad;t++){i64 d=i64(t)*(1LL<<c.d),p=0,a,b;
switch(c.shape){case 0:a=mul(kc,S,0)>>23;b=mul(a,d,1)>>23;p=mul(b,d,2);break;
case 1:a=mul(kc,d,0)>>23;b=mul(S,d,1)>>23;p=mul(a,b,2);break;
case 2:a=mul(kc,d,0)>>23;b=mul(a,S,1)>>23;p=mul(b,d,2);break;
case 3:a=mul(S,d,0)>>23;b=mul(a,d,1)>>23;p=mul(b,kc,2);break;
case 4:a=mul(kc,d,0)>>23;b=mul(a,i64(t)*(1LL<<c.d2),1)>>23;p=mul(b,S,2);break;}
if(bad)break;i64 q=sm(p,shift);for(int m: {n,32-n}){if(m==32-n&&n==16){} auto z=cells[m][t+512];i64 v=z.base-q;if(v<z.lo||v>z.hi){c.v++;c.pn[n]++;if(n<=2||n==16)c.g++;}if(n==16)break;}
if(c.v>400){bad=true;break;}}}if(bad)c.v=1000000;}
std::sort(cs.begin(),cs.end(),[](auto&a,auto&b){return a.v<b.v;});
auto print=[](const Cfg&c){printf("v=%d guard=%d f=%d k=%d d=%d tree=%d roles=%d p=%d d2=%d dk=%d |",c.v,c.g,c.f,c.k,c.d,c.shape,c.r,c.p,c.d2,c.dk);for(int x:c.pn)printf(" %d",x);puts("");};
for(int i=0;i<25;i++)print(cs[i]);puts("best per tree and best guard-clean per tree:");for(int s=0;s<5;s++){for(auto &c:cs)if(c.shape==s){print(c);break;}for(auto &c:cs)if(c.shape==s&&c.g==0&&c.v<1000000){print(c);break;}}
}
