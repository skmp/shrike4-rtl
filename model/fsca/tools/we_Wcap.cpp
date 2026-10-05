// we_Wcap.cpp -- NECESSARY-CONDITION ceiling on the latch granularity W.
// Writing the chain exactly as   J = floor((C*t^2 + c(t))/2^17),  c(t) = (E - r)*t + W*(64 - h),
// with r in [0,W), h in [0,64], E in [Elo,Ehi]  (E = (e1+K1)/2^13, the first array's offset),
// count the t whose required c window can be met AT ALL.  This bounds every chain with that W.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int Elo=argc>1?atoi(argv[1]):41, Ehi=argc>2?atoi(argv[2]):48;
    int hlo=argc>3?atoi(argv[3]):0, hhi=argc>4?atoi(argv[4]):64;
    printf("E in [%d,%d], h in [%d,%d]\n",Elo,Ehi,hlo,hhi);
    printf(" W      feasible-t (of 1022)   failing t (first 20)\n");
    for(int lw=7;lw<=14;lw++){ i64 W=1ll<<lw; int good=0; std::vector<int> bad;
        for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 q=(i64)KC*t*t;
            i64 cl=JLo[t+512]*(1ll<<17)-q, ch=(JHi[t+512]+1)*(1ll<<17)-1-q;
            // achievable c
            i64 alo,ahi;
            if(t>=0){ alo=(i64)(Elo-W+1)*t + W*(64-hhi); ahi=(i64)Ehi*t + W*(64-hlo); }
            else    { i64 a=-t; alo=-(i64)Ehi*a + W*(64-hhi); ahi=(i64)(W-1-Elo)*a + W*(64-hlo); }
            if(ahi>=cl&&alo<=ch) good++; else bad.push_back(t); }
        printf("2^%-2d   %4d                  ",lw,good);
        for(size_t i=0;i<bad.size()&&i<20;i++) printf("%d ",bad[i]); puts(""); }
    return 0; }
