// Diagnostic: redundant sum/carry feedback across two truncated multipliers.
#include "base2.h"
#include <algorithm>
#include <vector>
#include <cassert>
struct W{i64 l,h;};W w[1024];
struct C{int order1,tree,rev,inj,r,order2,k2,score,probe;};
static std::pair<i64,i64> pairprod(i64 rec,i64 mul,const C&c){
 std::vector<u64> rows; i64 R[14],T=0;
 for(int k=0;k<14;k++){auto bit=[&](int b){return b<0?0:int((rec>>b)&1);};int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);i64 mag=std::abs(d)*mul;R[k]=(d<0?~mag:mag)*(1ll<<(2*k));i64 r=(R[k]>>16)*65536;if(d<0&&2*k>=18)r+=1ll<<(2*k);rows.push_back(u64(r));T+=r;}
 i64 extra=65536*((((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1))>=2)-65536*(((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1);
 assert(T+extra==arr(rec,mul));rows.push_back(u64(extra));if(c.inj)rows.push_back(K);T+=extra+(c.inj?K:0);
 if(c.rev)std::reverse(rows.begin(),rows.end());
 auto compress=[](u64 a,u64 b,u64 d){return std::pair<u64,u64>{a^b^d,((a&b)|(a&d)|(b&d))*2};};
 if(c.tree==0){u64 s=0,k=0;for(auto x:rows){auto p=compress(s,k,x);s=p.first;k=p.second;}rows={s,k};}
 else while(rows.size()>2){std::vector<u64> nxt;size_t i=0;for(;i+2<rows.size();i+=3){auto p=compress(rows[i],rows[i+1],rows[i+2]);nxt.push_back(p.first);nxt.push_back(p.second);}for(;i<rows.size();i++)nxt.push_back(rows[i]);rows=nxt;}
 auto sign48=[](u64 x)->i64{x&=(1ull<<48)-1;return x&(1ull<<47)?i64(x)- (1ll<<48):i64(x);};i64 s=sign48(rows[0]),k=sign48(rows[1]);k+=T-s-k;assert(s+k==T);return {s,k};
}
i64 eval(int t,const C&c){i64 dx=i64(t)*8192;auto p=c.order1?pairprod(KC,dx,c):pairprod(dx,KC,c);i64 a=p.first>>c.r,b=p.second>>c.r;assert(std::abs(a)<(1ll<<27)&&std::abs(b)<(1ll<<27));i64 out=c.order2?arr(dx,a)+arr(dx,b):arr(a,dx)+arr(b,dx);return (out+c.k2*K)>>(41-c.r);}
int main(){FILE*f=fopen("jwin18.txt","r");if(!f)return 2;char line[256];int count=0,t,wd;long l,h;while(fgets(line,sizeof line,f))if(sscanf(line,"%d %ld %ld %d",&t,&l,&h,&wd)==4){w[t+512]={l,h};count++;}assert(count==1024);std::vector<C> v;int pass=0;for(int o=0;o<2;o++)for(int tr=0;tr<2;tr++)for(int rev=0;rev<2;rev++)for(int inj=0;inj<2;inj++)for(int r=22;r<=28;r++)for(int o2=0;o2<2;o2++)for(int k2=0;k2<=2;k2++){C c{o,tr,rev,inj,r,o2,k2,0,0};for(int t:{17,-17,342,-342}){auto j=eval(t,c);c.probe+=j>=w[t+512].l&&j<=w[t+512].h;}pass+=c.probe==4;for(int t=-512;t<512;t++){auto j=eval(t,c);c.score+=j>=w[t+512].l&&j<=w[t+512].h;}v.push_back(c);}std::sort(v.begin(),v.end(),[](C a,C b){return a.score>b.score;});printf("structures %zu pass all four %d\n",v.size(),pass);for(int i=0;i<12;i++){auto c=v[i];printf("hits%d probes%d order1%d tree%d rev%d inj%d r%d order2%d K2%d",c.score,c.probe,c.order1,c.tree,c.rev,c.inj,c.r,c.order2,c.k2);for(int t:{17,-17,342,-342})printf(" t%d=%ld",t,eval(t,c));puts("");}}
