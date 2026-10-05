// wf_l.cpp -- how much of the residual is explained by a per-CLASS integer offset on top of a base rule?
// Classes tested: octave of |t|, sign of t, t mod 8 and their products.  A class offset is the signature of
// a fixed rounding constant seen through a moving binary point (octave), or of the array's row truncation
// (t mod 8), or of a one's-complement convention (sign).  Each class gets one free integer, chosen optimally.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <map>
static i64 JL[1024],JH[1024]; static int OK[1024];
static int oct(int t){ int a=t<0?-t:t; int o=0; while(a>1){a>>=1;o++;} return o; }   // 0..9
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  const char* bn[5]={"floor(KC t^2/2^17)","round(KC t^2/2^17)","chain I1=(arr(KC,dx)+K)>>23,J=(arr(I1,dx)+K)>>20",
                     "(arr(KC,t*t)+2^16)>>17","(arr(KC,(t*t)<<6)+2^20)>>23"};
  const char* cn[8]={"none","sign","t mod 8","sign x (t mod 8)","octave","sign x octave","octave x (t mod 8)","sign x octave x (t mod 8)"};
  printf("base rule                                              class                        score /1022\n");
  for(int base=0;base<5;base++){
    std::vector<i64> J0(1024,0);
    for(int tt=-512;tt<=511;tt++){ i64 dx=(i64)tt<<13; i64 q=KC*(i64)tt*tt; i64 v=0;
      switch(base){ case 0: v=q>>17; break; case 1: v=(q+65536)>>17; break;
        case 2: { i64 I1=(arr(KC,dx)+K)>>23; v=(arr(I1,dx)+K)>>20; } break;
        case 3: v=(arr(KC,(i64)tt*tt)+65536)>>17; break;
        case 4: v=(arr(KC,((i64)tt*tt)<<6)+(1ll<<20))>>23; break; }
      J0[tt+512]=v; }
    for(int cl=0;cl<8;cl++){
      auto cid=[&](int tt)->int{ int s=tt<0?1:0, m=((tt%8)+8)%8, ov=oct(tt);
        switch(cl){ case 0: return 0; case 1: return s; case 2: return m; case 3: return s*8+m;
          case 4: return ov; case 5: return s*10+ov; case 6: return ov*8+m; default: return (s*10+ov)*8+m; } };
      int ncls=0; for(int tt=-512;tt<=511;tt++) ncls=std::max(ncls,cid(tt)+1);
      // per class: choose the offset covering the most t
      int total=0;
      for(int c=0;c<ncls;c++){ std::vector<std::pair<i64,int>> ev;
        for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512]||cid(tt)!=c)continue;
          ev.push_back({JL[tt+512]-J0[tt+512],1}); ev.push_back({JH[tt+512]-J0[tt+512]+1,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>mx)mx=cur;}
        total+=mx; }
      printf("%-54s %-28s %4d\n",bn[base],cn[cl],total); } }
  return 0; }
