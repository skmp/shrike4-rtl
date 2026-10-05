// we_pq.cpp -- exact numbers for the pinned t, and the best pure quadratic rule J = floor((Q*t^2+R)/2^m).
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    puts("pin    t     JL        JH      KC*t^2/2^17 (exact)      J-floor(ideal)   4pi^2 t^2 (long double)");
    for(int t : {40,-40,258,-258,342,-342,376,-376,512,-512,411,-411,447,-447,277,-277}){
        if(!JOK[t+512]) continue; i64 q=(i64)KC*(i64)t*t; i64 fl=q>>17; i64 rem=q-(fl<<17);
        printf("   %5d %9lld %9lld   %9lld + %6lld/2^17   %+4lld..%+4lld   %.3Lf\n",t,(long long)JLo[t+512],(long long)JHi[t+512],
            (long long)fl,(long long)rem,(long long)(JLo[t+512]-fl),(long long)(JHi[t+512]-fl),
            4.0L*3.14159265358979323846L*3.14159265358979323846L*(long double)t*t); }
    // best pure quadratic
    puts("\nbest pure quadratic  J = floor((Q*t^2+R)/2^m)  (symmetric by construction)");
    for(int m=17;m<=34;m++) for(int ri=0;ri<2;ri++){ i64 R=ri?(1ll<<(m-1)):0;
        std::vector<std::pair<i64,int>> ev;
        for(int t=-512;t<=511;t++){ if(!JOK[t+512]||!t) continue; i64 aa=(i64)t*t;
            i64 lo=JLo[t+512],hi=JHi[t+512];
            i64 qlo=(lo*(1ll<<m)-R+aa-1)/aa, qhi=((hi+1)*(1ll<<m)-1-R)/aa;
            if(qlo>qhi) continue; ev.push_back({qlo,1}); ev.push_back({qhi+1,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bq=0;
        for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best){best=cur;bq=ev[i].first;} }
        printf("  m=%2d R=%-12lld best=%4d  Q=%lld  Q/2^%d=%.9Lf (KC/2^17=%.9Lf, 4pi^2=%.9Lf)\n",m,(long long)R,best+1,(long long)bq,m,
            (long double)bq/powl(2.0L,m),(long double)KC/131072.0L,4.0L*3.14159265358979323846L*3.14159265358979323846L); }
    return 0; }
