// wf_b.cpp -- where the current chain fails, by how much, and with what chain internals.
#include "base2.h"
#include <cstring>
#include <map>
#include <vector>
#include <cmath>
static i64 JL[1024],JH[1024]; static int OKT[1024];
static void loadw(){FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);}
int main(){ build_tables(); loadw();
  int hit=0,lowfail=0,hifail=0; std::map<i64,int> defhist;
  printf("  t     JL       JH     Jmodel  miss   E1 r1/2^23  e2/2^13 r2/2^20   devlo    devhi    devmod  (all dev in 2^17 units)\n");
  std::vector<int> fails;
  for(int t=-512;t<=511;t++){ if(!OKT[t+512])continue; i64 dx=(i64)t<<13;
    i64 A1=arr(KC,dx); i64 I1=(A1+K)>>23; i64 r1=(A1+K)-(I1<<23); i64 e1=KC*dx-A1;
    i64 A2=arr(I1,dx); i64 I2=(A2+K)>>20; i64 r2=(A2+K)-(I2<<20); i64 e2=I1*dx-A2;
    i64 miss = I2<JL[t+512]? I2-JL[t+512] : (I2>JH[t+512]? I2-JH[t+512] : 0);
    if(miss==0){hit++;continue;}
    if(miss<0)lowfail++;else hifail++; defhist[miss]++; fails.push_back(t);
    double devlo=(JL[t+512]*131072.0-(double)KC*t*t)/131072.0, devhi=(JH[t+512]*131072.0-(double)KC*t*t)/131072.0;
    double devm=(I2*131072.0-(double)KC*t*t)/131072.0;
    if(fails.size()<=200) printf("%5d %8lld %8lld %8lld %5lld  %3lld %8.5f %6lld %8.5f %8.3f %8.3f %8.3f\n",
      t,(long long)JL[t+512],(long long)JH[t+512],(long long)I2,(long long)miss,
      (long long)(e1>>13),(double)r1/8388608.0,(long long)(e2>>13),(double)r2/1048576.0,devlo,devhi,devm);
  }
  printf("\nhits %d, fails %zu (model too low %d, too high %d)\n",hit,fails.size(),lowfail,hifail);
  printf("miss histogram:"); for(auto&p:defhist)printf(" %lld:%d",(long long)p.first,p.second); printf("\n");
  printf("failing t: "); for(int t:fails)printf("%d ",t); printf("\n");
  return 0; }
