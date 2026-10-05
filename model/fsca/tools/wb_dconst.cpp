// wb_dconst.cpp -- is the required asymmetry D(|t|) = J(-t)-J(t) consistent with ONE constant?
// exact interval intersection over all |t| with both windows non-empty; also the best constant and
// the best affine forms D = a + b*|t|/2^m.
#include "base2.h"
#include <cstring>
#include <vector>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ FILE*f=fopen(argc>1?argv[1]:"jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    i64 lo=-1000,hi=1000; int n=0; std::vector<std::pair<i64,i64>> iv;
    for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!OKT[ip]||!OKT[im])continue; n++;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip]; iv.push_back({dlo,dhi});
        if(dlo>lo)lo=dlo; if(dhi<hi)hi=dhi; }
    printf("pairs: %d   intersection of all required D intervals: [%lld,%lld] %s\n",n,(long long)lo,(long long)hi,
        lo<=hi?"<== A CONSTANT D WORKS":"(empty)");
    // best constant by count
    int bestc=0,bestD=0; for(i64 D=-20;D<=40;D++){ int c=0; for(auto&p:iv) if(D>=p.first&&D<=p.second)c++;
        if(c>bestc){bestc=c;bestD=(int)D;} }
    printf("best single constant D=%d satisfies %d of %d pairs\n",bestD,bestc,n);
    for(i64 D=0;D<=12;D++){ int c=0; for(auto&p:iv) if(D>=p.first&&D<=p.second)c++;
        printf("   D=%2lld : %d/%d\n",(long long)D,c,n); }
    // which |t| fail for the best constant
    printf("failing |t| for D=%d: ",bestD); int k=0;
    for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip]; if(bestD<dlo||bestD>dhi){ printf("%d[%lld,%lld] ",A,(long long)dlo,(long long)dhi); if(++k>40)break; } }
    printf("\n");
    return 0; }
