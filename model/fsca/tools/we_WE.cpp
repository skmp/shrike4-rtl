// we_WE.cpp -- ceiling over (W, E): J = floor((KC t^2 + c)/2^17), c = (E-r)t + W*(64-h),
// r in [0,W) free, h in [0,64] free, E a FIXED integer (the first latch's rounding offset).
// This upper-bounds every two-multiply chain with that granularity and rounding constant.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    printf("  W    bestE   ceiling(of 1022)    E=46 ceiling\n");
    for(int lw=8;lw<=14;lw++){ i64 W=1ll<<lw; int bg=-1; i64 bE=0; int g46=0;
        for(i64 E=0;E<W;E++){ int good=0;
            for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 q=(i64)KC*t*t;
                i64 cl=JLo[t+512]*(1ll<<17)-q, ch=(JHi[t+512]+1)*(1ll<<17)-1-q;
                i64 alo,ahi;
                if(t>=0){ alo=(E-W+1)*(i64)t; ahi=E*(i64)t + 64*W; }
                else { i64 a=-t; alo=-E*a; ahi=(W-1-E)*a + 64*W; }
                if(ahi>=cl&&alo<=ch) good++; }
            if(good>bg){bg=good;bE=E;} if(E==46)g46=good; }
        printf("2^%-2d  %6lld   %4d                %4d\n",lw,(long long)bE,bg,g46); }
    // for the best (W,E) print which t still fail
    for(int lw=10;lw<=13;lw++){ i64 W=1ll<<lw; int bg=-1; i64 bE=0;
        for(i64 E=0;E<W;E++){ int good=0;
            for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 q=(i64)KC*t*t;
                i64 cl=JLo[t+512]*(1ll<<17)-q, ch=(JHi[t+512]+1)*(1ll<<17)-1-q; i64 alo,ahi;
                if(t>=0){ alo=(E-W+1)*(i64)t; ahi=E*(i64)t + 64*W; } else { i64 a=-t; alo=-E*a; ahi=(W-1-E)*a+64*W; }
                if(ahi>=cl&&alo<=ch) good++; }
            if(good>bg){bg=good;bE=E;} }
        printf("W=2^%d E=%lld ceiling=%d ; failing t:",lw,(long long)bE,bg);
        int n=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 q=(i64)KC*t*t;
            i64 cl=JLo[t+512]*(1ll<<17)-q, ch=(JHi[t+512]+1)*(1ll<<17)-1-q; i64 alo,ahi;
            if(t>=0){ alo=(bE-W+1)*(i64)t; ahi=bE*(i64)t + 64*W; } else { i64 a=-t; alo=-bE*a; ahi=(W-1-bE)*a+64*W; }
            if(!(ahi>=cl&&alo<=ch)){ printf(" %d",t); if(++n>25){printf(" ...");break;} } } puts(""); }
    return 0; }
