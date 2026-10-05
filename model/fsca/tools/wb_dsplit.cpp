// wb_dsplit.cpp -- if the asymmetry is "one LSB of a first-stage latch", D(|t|) is either 0 or q(|t|)=|t|*2^-m.
// For each m, split the 510 |t| into MUST-FIRE (Dlo>0 and q fits), MUST-NOT-FIRE (q > Dhi), FREE, and
// IMPOSSIBLE (Dlo>0 but q outside [Dlo,Dhi]).
#include "base2.h"
#include <cstring>
#include <vector>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ build_tables();
    FILE*f=fopen(argc>1?argv[1]:"jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    puts(" m  q(t)=t/2^m | must-fire must-not free impossible | fire-rate needed");
    for(int m=4;m<=9;m++){ int mf=0,mn=0,fr=0,imp=0; std::vector<int> impl,mfl;
        for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!OKT[ip]||!OKT[im])continue;
            i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
            i64 qf=A>>m, qc=(A+(1<<m)-1)>>m;                       // floor/ceil of |t|/2^m
            bool qfit=(qf>=dlo&&qf<=dhi)||(qc>=dlo&&qc<=dhi);
            bool zfit=(dlo<=0&&0<=dhi);
            if(dlo>0){ if(qfit){mf++; mfl.push_back(A);} else {imp++; impl.push_back(A);} }
            else if(!qfit) mn++;
            else fr++; }
        printf("%2d            | %6d %8d %5d %9d |  %.1f%%",m,mf,mn,fr,imp,100.0*mf/(mf+mn+fr+imp));
        if(imp){ printf("   impossible: "); for(size_t i=0;i<impl.size()&&i<12;i++)printf("%d ",impl[i]); }
        printf("\n"); }
    return 0; }
