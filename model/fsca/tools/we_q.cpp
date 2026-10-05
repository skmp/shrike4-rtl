// we_q.cpp -- (A) exact per-t miss report of the current chain; (B) best achievable score with an EVEN J;
// (C) exhaustive exact search for the best pure symmetric rule J = (Q*t^2 + R) >> m  (interval inversion on Q);
// (D) the 4 exactly-pinned |t| printed in full.
#include "we_win.h"
#include <cstring>
#include <algorithm>
#include <map>
static inline i64 Jmod(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    // (A)
    int below=0,above=0,inn=0; std::map<i64,int> hist;
    int belowp=0,belowm=0,abovep=0,abovem=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 j=Jmod(t);
        if(j<JLo[t+512]){below++; hist[j-JLo[t+512]]++; if(t>0)belowp++; else belowm++;}
        else if(j>JHi[t+512]){above++; hist[j-JHi[t+512]]++; if(t>0)abovep++; else abovem++;}
        else inn++; }
    printf("(A) chain: in %d, below JL %d (t>0:%d t<0:%d), above JH %d (t>0:%d t<0:%d)\n",inn,below,belowp,belowm,above,abovep,abovem);
    printf("    miss-size histogram (signed distance outside the window):");
    for(auto&p:hist) printf(" %lld:%d",(long long)p.first,p.second); puts("");
    // (B) best even-J score
    int evenscore=0, ndisj=0;
    for(int a=0;a<=512;a++){ int ip=a+512, im=-a+512; bool okp = a<=511 && JOK[ip], okm = JOK[im];
        if(a==0){ evenscore+= okm?1:0; continue; }
        if(okp&&okm){ i64 lo=std::max(JLo[ip],JLo[im]), hi=std::min(JHi[ip],JHi[im]);
            if(lo<=hi) evenscore+=2; else {evenscore+=1; ndisj++;} }
        else if(okp||okm) evenscore+=1; }
    printf("(B) best even-J score: %d (disjoint pairs: %d)  [max 1022]\n",evenscore,ndisj);
    // (C) symmetric rule J = (Q*a^2 + R) >> m : intersect Q intervals, sweep for max coverage
    printf("(C) best pure symmetric floor rules:\n");
    for(int m=10;m<=40;m++){
      for(int ri=0;ri<3;ri++){
        i64 R = ri==0?0 : ri==1?(1ll<<(m-1)) : (1ll<<(m-2));
        std::vector<std::pair<i64,int>> ev;
        for(int a=1;a<=512;a++){ int ip=a+512, im=-a+512;
            i64 lo=-1,hi=-2;
            bool okp = a<=511&&JOK[ip], okm=JOK[im];
            if(okp&&okm){ lo=std::max(JLo[ip],JLo[im]); hi=std::min(JHi[ip],JHi[im]); }
            else if(okp){lo=JLo[ip];hi=JHi[ip];} else if(okm){lo=JLo[im];hi=JHi[im];} else continue;
            if(lo>hi) continue;
            i64 aa=(i64)a*a;
            // floor((Q*aa+R)/2^m) in [lo,hi]  <=>  Q*aa+R in [lo*2^m, (hi+1)*2^m-1]
            i64 qlo = (lo*(1ll<<m) - R + aa-1)/aa; if(lo*(1ll<<m)-R < 0) qlo = -(( -(lo*(1ll<<m)-R))/aa);
            i64 qhi = ((hi+1)*(1ll<<m)-1-R)/aa;
            int w = (okp&&okm)?2:1;
            ev.push_back({qlo,w}); ev.push_back({qhi+1,-w}); }
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bq=0;
        for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best){best=cur;bq=ev[i].first;} }
        int total=best + (JOK[512]?1:0);   // t=0
        if(total>=960) printf("    m=%2d R=%-12lld best=%d  Q=%lld  (Q/2^m=%.9Lf)\n",m,(long long)R,total,(long long)bq,(long double)bq/powl(2.0L,m));
      }
    }
    // (D)
    printf("(D) exactly pinned:\n");
    int pins[4]={40,258,342,376};
    for(int k=0;k<4;k++){int a=pins[k];
        printf("  a=%3d : J(+a) in [%lld,%lld] model %lld ; J(-a) in [%lld,%lld] model %lld\n",a,
        (long long)JLo[a+512],(long long)JHi[a+512],(long long)Jmod(a),
        (long long)JLo[-a+512],(long long)JHi[-a+512],(long long)Jmod(-a)); }
    return 0; }
