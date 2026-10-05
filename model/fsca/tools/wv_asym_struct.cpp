// wv_asym_struct.cpp -- adversarial verification, part 7: the structural ceilings of claim item 5.
//  (a) the granularity window for D(342) at each W=2^p
//  (b) the W=2048 first-latch-offset scan (claimed: no E satisfies both +-342; best jwin 902)
//  (g) the best pure symmetric quadratic J = floor((Q t^2 + R)/2^m)   (claimed 917..923)
#include "wv_asym_win.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static inline i64 fdiv(i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)!=(b<0)))q--; return q; }
int main(){ if(!load_fsca(WVF))return 1; build_tables(); wv_build(0);
    // (a) granularity: one first-latch LSB at a = 342 is W*342/2^17, slack +-W*64/2^17
    { int ip=342+512,im=-342+512; i64 dlo=WV_LO[im]-WV_HI[ip],dhi=WV_HI[im]-WV_LO[ip];
      printf("D(342) required in [%lld,%lld]\n",(long long)dlo,(long long)dhi);
      for(int p=8;p<=14;p++){ long long W=1ll<<p; long double c=(long double)W*342/131072.0L, s=(long double)W*64/131072.0L;
        bool hit = (c+s>=dlo) && (c-s<=dhi);
        printf("  W=2^%-2d : delta=1 gives D in [%.2Lf,%.2Lf]  %s\n",p,c-s,c+s,hit?"<= can reach the window":""); } }
    // (b) W = 2048 (s1=24, s2=19): I1 = floor((KC t + E)/2048), J = (arr(I1,t<<13)+2^19)>>19
    { int best=-1,bestE=-1,both=0;
      for(int E=0;E<2048;E++){ int sc=0; bool p342=false,m342=false;
        for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue; i64 I1=fdiv(KC*(i64)t+E,2048);
          i64 J=(arr(I1,(i64)t<<13)+(1ll<<19))>>19;
          bool ok=(J>=WV_LO[t+512]&&J<=WV_HI[t+512]); if(ok)sc++;
          if(t==342)p342=ok; if(t==-342)m342=ok; }
        if(p342&&m342) both++;
        if(sc>best){best=sc;bestE=E;} }
      printf("W=2048 single-offset scan: best jwin = %d at E = %d ; offsets satisfying BOTH +-342 = %d / 2048\n",best,bestE,both); }
    // (g) symmetric quadratic
    { printf("best symmetric quadratic  J = floor((Q t^2 + R)/2^m):\n");
      for(int m=17;m<=26;m++){ i64 Qc=KC<<(m-17); int bestsc=0; i64 bq=0,br=0;
        #pragma omp parallel
        { int lb=0; i64 lq=0,lr=0;
          #pragma omp for schedule(dynamic)
          for(i64 dq=-3000;dq<=3000;dq++){ i64 Q=Qc+dq; std::vector<std::pair<i64,int>> ev; ev.reserve(2100);
            for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue; i64 q=Q*(i64)t*(i64)t;
              i64 lo=(WV_LO[t+512]<<m)-q, hi=((WV_HI[t+512]+1)<<m)-1-q;
              ev.push_back({lo,+1}); ev.push_back({hi+1,-1}); }
            std::sort(ev.begin(),ev.end()); int cur=0,mx=0; i64 at=0;
            for(auto&e:ev){ cur+=e.second; if(cur>mx){mx=cur;at=e.first;} }
            if(mx>lb){lb=mx;lq=Q;lr=at;} }
          #pragma omp critical
          { if(lb>bestsc){bestsc=lb;bq=lq;br=lr;} } }
        printf("  m=%2d : best %d/1022  Q=%lld  R=%lld   Q/2^m = %.7Lf\n",m,bestsc,(long long)bq,(long long)br,(long double)bq/(long double)(1ll<<m)); } }
    printf("reference: KC/2^17 = %.7Lf , 4*pi^2 = %.7Lf\n",(long double)KC/131072.0L,4.0L*3.14159265358979323846L*3.14159265358979323846L);
    return 0; }
