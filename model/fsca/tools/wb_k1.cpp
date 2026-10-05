// wb_k1.cpp -- for a fixed second stage (s1,s2,K2,role), invert to the allowed I1 interval per t, turn it
// into the exact allowed interval for the first rounding constant K1 (floor rule), and sweep for the K1 that
// satisfies the most t.  Prints the coverage profile and the t that conflict with the best K1.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ build_tables();
    FILE*f=fopen("jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    int s1=argc>1?atoi(argv[1]):23, s2=argc>2?atoi(argv[2]):20; i64 K2=argc>3?atoll(argv[3]):0;
    int role=argc>4?atoi(argv[4]):0; int verbose=argc>5?atoi(argv[5]):0;
    std::vector<std::pair<i64,i64>> kv; std::vector<int> kt; int nonempty=0;
    std::vector<i64> ILO(1024),IHI(1024); std::vector<char> ok(1024,0);
    for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 dx=(i64)tt<<13;
        long double c=(long double)KC*(long double)tt*8192.0L/powl(2.0L,(long double)s1);
        i64 lo0=(i64)floorl(c)-4000, hi0=(i64)ceill(c)+4000; i64 fi=0,gi=0; bool any=false;
        for(i64 v=lo0;v<=hi0;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=(P+K2)>>s2;
            if(J>=JL[tt+512]&&J<=JH[tt+512]){ if(!any){fi=v;any=true;} gi=v; } }
        if(!any) continue;
        nonempty++; ILO[tt+512]=fi; IHI[tt+512]=gi; ok[tt+512]=1;
        i64 P1=arr(KC,dx);
        kv.push_back({fi*(1ll<<s1)-P1, (gi+1)*(1ll<<s1)-1-P1}); kt.push_back(tt); }
    printf("s1=%d s2=%d K2=%lld role=%d : %d/1022 t reachable by some I1\n",s1,s2,(long long)K2,role,nonempty);
    std::vector<std::pair<i64,int>> ev; for(auto&p:kv){ ev.push_back({p.first,1}); ev.push_back({p.second+1,-1}); }
    std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bl=0,bh=0;
    for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best&&i+1<ev.size()){best=cur;bl=ev[i].first;bh=ev[i+1].first-1;} }
    printf("best K1 window [%lld,%lld] covers %d of %d  (2^19=%d 2^20=%d 2^21=%d 2^22=%d)\n",
        (long long)bl,(long long)bh,best,(int)kv.size(),1<<19,1<<20,1<<21,1<<22);
    // conflicts with the best K1
    i64 K1=bl; int nb=0; printf("t failing with K1=%lld: ",(long long)K1);
    for(size_t i=0;i<kv.size();i++) if(K1<kv[i].first||K1>kv[i].second){ if(nb<40) printf("%d ",kt[i]); nb++; }
    printf("  (%d total)\n",nb);
    if(verbose){ puts("t : allowed K1 interval (only the failing ones)");
        for(size_t i=0;i<kv.size();i++) if(K1<kv[i].first||K1>kv[i].second)
            printf("%5d : [%12lld,%12lld]  I1 in [%lld,%lld]\n",kt[i],(long long)kv[i].first,(long long)kv[i].second,
                (long long)ILO[kt[i]+512],(long long)IHI[kt[i]+512]); }
    return 0; }
