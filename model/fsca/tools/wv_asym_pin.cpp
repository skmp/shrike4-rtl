// wv_asym_pin.cpp -- the tight-window pin data (claim item 7): windows with JHi-JLo <= 2.
#include "wv_asym_win.h"
#include <cstdio>
int main(){ if(!load_fsca(WVF))return 1; build_tables(); wv_build(0);
    int n=0; for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue; if(WV_HI[t+512]-WV_LO[t+512]>2)continue;
        i64 fl=(KC*(i64)t*(i64)t)>>17;
        printf("t=%+4d  J-fl in [%+lld,%+lld]  (JL=%lld JH=%lld)\n",t,(long long)(WV_LO[t+512]-fl),
            (long long)(WV_HI[t+512]-fl),(long long)WV_LO[t+512],(long long)WV_HI[t+512]); n++; }
    printf("count of t with JHi-JLo <= 2 : %d\n",n); return 0; }
