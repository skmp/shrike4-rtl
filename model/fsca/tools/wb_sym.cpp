// wb_sym.cpp -- best SYMMETRIC rule for J: one multiply of a constant with the exact t^2 latch.
// J = R((arr(A, t^2<<g) or arr(t^2<<g, A)) + K2) >> s2.  Scan A exhaustively near the ideal.
// The symmetric ceiling from the data is 932; how close does a clean form get?
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(){ build_tables();
    FILE*f=fopen("jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    struct R{int sc; i64 A; int g,s2,role,k2;};
    std::vector<R> best;
    for(int g=0;g<=8;g++) for(int s2=10;s2<=34;s2++){
        // ideal A: J = A*t^2*2^g/2^s2 = KC t^2/2^17  ->  A = KC*2^(s2-g-17)
        long double idl=(long double)KC*powl(2.0L,(long double)(s2-g-17));
        if(idl<1000||idl>(long double)(1ll<<27)) continue;
        i64 A0=(i64)llroundl(idl);
        for(int role=0;role<2;role++) for(int k2=0;k2<4;k2++){
            i64 K2 = k2==0?0 : k2==1?(1ll<<19) : k2==2?(s2>0?(1ll<<(s2-1)):0) : (s2>1?(1ll<<(s2-2)):0);
            int bs=-1; i64 bA=0;
            #pragma omp parallel for schedule(static)
            for(i64 A=A0-300;A<=A0+300;A++){ int sc=0;
                for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 q=((i64)tt*tt)<<g;
                    i64 P=role?arr(q,A):arr(A,q); i64 J=(P+K2)>>s2;
                    if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
                #pragma omp critical
                { if(sc>bs){bs=sc;bA=A;} } }
            best.push_back({bs,bA,g,s2,role,k2}); } }
    std::sort(best.begin(),best.end(),[](const R&x,const R&y){return x.sc>y.sc;});
    puts("score   A           g  s2 role k2");
    for(int i=0;i<20&&i<(int)best.size();i++) printf("%5d  %10lld  %2d %2d  %d   %d\n",
        best[i].sc,(long long)best[i].A,best[i].g,best[i].s2,best[i].role,best[i].k2);
    return 0; }
