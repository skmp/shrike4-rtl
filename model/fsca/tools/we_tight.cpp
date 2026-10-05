// we_tight.cpp -- all t whose allowed-J window is narrow, with the residual against floor(KC t^2/2^17)
// and against the current chain.  These are the hard data the rule must reproduce.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int wmax=argc>1?atoi(argv[1]):3;
    printf("# t   JL   JH  w  fl=floor(KC t^2/2^17) frac  JL-fl JH-fl  model-fl  r1   h\n");
    int n=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512]||!t)continue; i64 w=JHi[t+512]-JLo[t+512]; if(w>wmax)continue;
        i64 q=(i64)KC*t*t; i64 fl=q>>17; i64 fr=q-(fl<<17);
        i64 dx=(i64)t<<13; i64 e1=arr(KC,dx)-(i64)KC*dx; i64 g=(-e1/8192)-16;
        i64 num=(i64)KC*t+48-g; i64 I1=num>>10; i64 r1=num-(I1<<10);
        i64 e2=arr(I1,dx)-I1*dx; i64 h=-e2/8192; i64 J=(arr(I1,dx)+K)>>20;
        printf("%5d %9lld %9lld %2lld  %9lld %6.3f  %+4lld %+4lld  %+4lld  %4lld %3lld\n",t,
            (long long)JLo[t+512],(long long)JHi[t+512],(long long)w,(long long)fl,(double)fr/131072.0,
            (long long)(JLo[t+512]-fl),(long long)(JHi[t+512]-fl),(long long)(J-fl),(long long)r1,(long long)h);
        n++; }
    printf("count=%d\n",n); return 0; }
