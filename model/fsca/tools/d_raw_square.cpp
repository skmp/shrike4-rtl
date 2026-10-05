// Test retained fractional bits of the raw Booth squarer, not an exact t*t latch.
#include "base2.h"
#include <algorithm>
#include <vector>
struct W{i64 l,h;}; W w[1024];
struct C{int a,b,r,h,order,k1,k2,score,probe;};
i64 eval(int t,const C&c){i64 p=arr(i64(t)*(1ll<<c.a),i64(t)*(1ll<<c.b));i64 x=(p+c.k1*K)>>c.r; i64 k=KC*(1ll<<c.h);return ((c.order?arr(k,x):arr(x,k))+c.k2*K)>>(c.a+c.b+15+c.h-c.r);}
int main(){int asymmetric=0;for(int t=1;t<512;t++){i64 x=i64(t)*8192;asymmetric+=arr(x,x)!=arr(-x,-x);if(t==17||t==342||t==385)printf("raw t%d positive_error%ld negative_error%ld\n",t,arr(x,x)-x*x,arr(-x,-x)-x*x);}printf("raw squarer asymmetric pairs %d / 511\n",asymmetric);FILE*f=fopen("jwin18.txt","r");if(!f)return 2;int t,wd;long l,h;char line[256];int loaded=0;while(fgets(line,sizeof line,f)){if(sscanf(line,"%d %ld %ld %d",&t,&l,&h,&wd)==4){w[t+512]={l,h};loaded++;}}fclose(f);if(loaded!=1024)return 3;std::vector<C> v;int count=0,pass=0;
for(int a=8;a<=14;a++)for(int b=8;b<=14;b++)for(int r=std::max(0,a+b-8);r<=a+b+2;r++)for(int sh=0;sh<=2;sh++)for(int o=0;o<2;o++)for(int k1=0;k1<=1;k1++)for(int k2=0;k2<=1;k2++){
 C c{a,b,r,sh,o,k1,k2,0,0};count++;for(int t:{17,-17,342,-342}){i64 j=eval(t,c);c.probe+=j>=w[t+512].l&&j<=w[t+512].h;}pass+=c.probe==4;
 for(int t=-512;t<512;t++){i64 j=eval(t,c);c.score+=j>=w[t+512].l&&j<=w[t+512].h;}v.push_back(c);
}std::sort(v.begin(),v.end(),[](C a,C b){return a.score>b.score;});printf("structures %d pass all four probes %d\n",count,pass);for(int i=0;i<12;i++){auto c=v[i];printf("hits %d probes %d a%d b%d r%d KCshift%d order%d K%d,%d",c.score,c.probe,c.a,c.b,c.r,c.h,c.order,c.k1,c.k2);for(int t:{17,-17,342,-342})printf(" t%d=%ld",t,eval(t,c));puts("");}
}
