// wc_pin.cpp -- which node instances actually pin the square latch at the tight offsets?  A pin that
// rests on a single node is only as good as that node's linear/cubic/C0 terms.
#include "wc_core.h"
#include <vector>
#include <algorithm>
int main(int argc,char**argv){ wc_init();
    int ts[] = {40,-40,228,-228,258,-258,342,-342,376,-376,417,-417,503,-503,17,-17};
    for(int t : ts){
        i64 gl=0,gh=12000000; printf("t=%d :\n",t);
        struct R{int node; i64 lo,hi; };
        std::vector<R> rs;
        for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue; const Tab&T=TB[n];
            i64 base=C0B[n]+LT[node][t+512];
            i64 t2lo=base-WHI[node][t+512], t2hi=base-WLO[node][t+512]; int u=T.u2;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            rs.push_back({node,a1,a2}); if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        printf("   intersection [%lld,%lld]\n",(long long)gl,(long long)gh);
        int nlo=0,nhi=0;
        for(auto&r:rs){ bool bl=(r.lo==gl), bh=(r.hi==gh); if(bl)nlo++; if(bh)nhi++;
            if(bl||bh) printf("   node %2d binds %s : [%lld,%lld]  (window exact? %d)\n",r.node,
                bl&&bh?"BOTH":(bl?"lo":"hi"),(long long)r.lo,(long long)r.hi,
                (int)(WLO[r.node][t+512]==WHI[r.node][t+512])); }
        printf("   #nodes binding lo=%d hi=%d\n",nlo,nhi); }
    return 0; }
