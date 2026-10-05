// wb_sym18.cpp -- best SYMMETRIC rule for the FINE latch: J = R((arr(A, t^2<<g) | arr(t^2<<g, A)) + K2) >> s2,
// scanning A exhaustively, K2 over all multiples of 2^16 below 2^s2 plus 2^19, and s2 down to 12 where the
// array's row truncation is worth the 15-20 units the data demands.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ build_tables();
    FILE*f=fopen(argc>1?argv[1]:"wb_jwin18f.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    struct R{int sc; i64 A,K2; int g,s2,role;}; std::vector<R> best;
    for(int g=0;g<=6;g++) for(int s2=10;s2<=26;s2++){
        long double idl=(long double)KC*powl(2.0L,(long double)(s2-g-15));
        if(idl<10000||idl>(long double)(1ll<<27)) continue; i64 A0=(i64)llroundl(idl);
        std::vector<i64> K2s; for(int k=0;k<(1<<((s2-16)>0?(s2-16>8?8:s2-16):0));k++)K2s.push_back((i64)k<<16);
        K2s.push_back(1ll<<19); K2s.push_back(1ll<<18); K2s.push_back(1ll<<17); K2s.push_back(0);
        std::sort(K2s.begin(),K2s.end()); K2s.erase(std::unique(K2s.begin(),K2s.end()),K2s.end());
        for(int role=0;role<2;role++) for(i64 K2:K2s){
            int bs=-1; i64 bA=0;
            #pragma omp parallel for schedule(static)
            for(i64 A=A0-400;A<=A0+400;A++){ int sc=0;
                for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 q=((i64)tt*tt)<<g;
                    i64 P=role?arr(q,A):arr(A,q); i64 J=(P+K2)>>s2;
                    if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
                #pragma omp critical
                { if(sc>bs){bs=sc;bA=A;} } }
            best.push_back({bs,bA,K2,g,s2,role}); } }
    std::sort(best.begin(),best.end(),[](const R&x,const R&y){return x.sc>y.sc;});
    puts("score/1024   A           K2        g  s2 role");
    for(int i=0;i<15&&i<(int)best.size();i++) printf("%5d  %11lld %9lld  %2d %2d  %d\n",
        best[i].sc,(long long)best[i].A,(long long)best[i].K2,best[i].g,best[i].s2,best[i].role);
    return 0; }
