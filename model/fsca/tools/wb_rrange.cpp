// wb_rrange.cpp -- the asymmetry quantum as a CONTINUOUS rate: D(|t|) in {0, round(r*|t|)}.  For every |t|
// that forces D>0, the required D interval bounds r; intersect exactly (rational arithmetic via integers).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ build_tables();
    FILE*f=fopen(argc>1?argv[1]:"jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    // r must satisfy: exists integer d in [Dlo,Dhi] with d = floor(r|t|) or ceil(r|t|)
    //  => r*|t| in (Dlo-1, Dhi+1)  (open)
    long double rlo=0.0L, rhi=1.0L; int nb=0; int blo=0,bhi=0;
    for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip]; if(dlo<=0)continue; nb++;
        long double lo=(long double)(dlo-1)/A, hi=(long double)(dhi+1)/A;
        if(lo>rlo){rlo=lo;blo=A;} if(hi<rhi){rhi=hi;bhi=A;} }
    printf("must-fire |t|: %d\n",nb);
    printf("allowed rate r in (%.8Lf, %.8Lf)   binding |t|: low %d, high %d\n",rlo,rhi,blo,bhi);
    printf("  1/64 = %.8f  %s\n",1.0/64,(1.0/64>rlo&&1.0/64<rhi)?"IN":"out");
    printf("  1/128= %.8f  %s\n",1.0/128,(1.0/128>rlo&&1.0/128<rhi)?"IN":"out");
    printf("  1/32 = %.8f  %s\n",1.0/32,(1.0/32>rlo&&1.0/32<rhi)?"IN":"out");
    printf("  1/48 = %.8f  %s\n",1.0/48,(1.0/48>rlo&&1.0/48<rhi)?"IN":"out");
    printf("  1/96 = %.8f  %s\n",1.0/96,(1.0/96>rlo&&1.0/96<rhi)?"IN":"out");
    // report the tightest few
    puts("tightest |t| constraints (r range each):");
    std::vector<std::pair<long double,int>> v;
    for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip]; if(dlo<=0)continue;
        v.push_back({(long double)(dhi+1-dlo+1)/A,A}); }
    std::sort(v.begin(),v.end());
    for(int i=0;i<6&&i<(int)v.size();i++){ int A=v[i].second; int ip=A+512,im=-A+512;
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        printf("  |t|=%3d D in [%lld,%lld] -> r in (%.6Lf,%.6Lf)\n",A,(long long)dlo,(long long)dhi,
            (long double)(dlo-1)/A,(long double)(dhi+1)/A); }
    return 0; }
