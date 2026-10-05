// wb_corr.cpp -- take the best symmetric rule Jsym(t) = arr(2t^2, A)>>17 (A=2586787) and dump the required
// correction interval c(t) = [JL-Jsym, JH-Jsym] for every t it misses, plus summary statistics.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(int argc,char**argv){ build_tables();
    const char*fn=argc>1?argv[1]:"jwin.txt"; i64 A=argc>2?atoll(argv[2]):2586787;
    FILE*f=fopen(fn,"r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    int sc=0,mp=0,mn=0; std::vector<int> misspos,missneg;
    puts("   t    Jsym   clo  chi   wid | mirror clo chi");
    for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 J=arr(((i64)tt*tt)<<1,A)>>17;
        i64 clo=JL[tt+512]-J, chi=JH[tt+512]-J;
        if(clo<=0&&0<=chi){sc++;continue;}
        int mt=-tt; i64 mlo=0,mhi=0; bool mok=OKT[mt+512];
        if(mok){ i64 Jm=arr(((i64)mt*mt)<<1,A)>>17; mlo=JL[mt+512]-Jm; mhi=JH[mt+512]-Jm; }
        printf("%5d %9lld %5lld %5lld %5lld | %5lld %5lld\n",tt,(long long)J,(long long)clo,(long long)chi,
            (long long)(chi-clo),(long long)mlo,(long long)mhi);
        if(tt>0){mp++;misspos.push_back(tt);} else {mn++;missneg.push_back(tt);} }
    printf("score %d/1022 ; misses: t>0 %d, t<0 %d\n",sc,mp,mn);
    return 0; }
