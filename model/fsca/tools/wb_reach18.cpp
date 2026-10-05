// wb_reach18.cpp -- at the FINE latch scale (jwin18, J ~ 157.88 t^2) the asymmetry quantum that fits is
// |t|/16, i.e. one LSB of a first latch L ~ 2526 t entering J through  J = (arr(L,dx)+K2) >> 17.
// Is every t then REACHABLE?  (At the coarse scale the same quantum left 8-12 t unreachable.)
#include "base2.h"
#include <cstring>
#include <vector>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ build_tables();
    const char*fn=argc>1?argv[1]:"wb_jwin18f.txt"; long double SC=argc>2?atof(argv[2]):157.8848L;
    FILE*f=fopen(fn,"r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    puts("s2 role K2/2^16 | reach   unreachable t (miss)");
    for(int s2=15;s2<=20;s2++) for(int role=0;role<2;role++){
        int nk=1<<((s2-16)>0?s2-16:0);
        for(int k=0;k<nk;k++){ i64 K2=(i64)k<<16; int reach=0; std::vector<int> un; std::vector<i64> md;
            for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 dx=(i64)tt<<13;
                i64 c0=(i64)llroundl(SC*(long double)tt*powl(2.0L,(long double)s2-13.0L));
                bool any=false; i64 bd=1ll<<60;
                for(i64 v=c0-3000;v<=c0+3000;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=(P+K2)>>s2;
                    if(J>=JL[tt+512]&&J<=JH[tt+512]){any=true;break;}
                    i64 d=J<JL[tt+512]?JL[tt+512]-J:J-JH[tt+512]; if(d<bd)bd=d; }
                if(any)reach++; else {un.push_back(tt); md.push_back(bd);} }
            printf("%2d  %d   %3d     | %4d  ",s2,role,k,reach);
            for(size_t i=0;i<un.size()&&i<8;i++) printf("%d(%lld) ",un[i],(long long)md[i]);
            printf("%s\n",reach>=1024?"<== ALL REACHABLE":""); fflush(stdout); } }
    return 0; }
