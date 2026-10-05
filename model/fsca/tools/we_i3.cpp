// we_i3.cpp -- detail of the required I1 intervals under the CURRENT second stage:
// which t are infeasible, and the residual  IL/IH - floor((KC*t+48-g)/1024)  per t.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static const int GT[8]={0,5,2,7,4,1,6,3};
static inline i64 I1rule(int t){ int m=((t%8)+8)%8; return ((i64)KC*t + 48 - GT[m]) >> 10; }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int nempty=0,nin=0;
    printf("# t   IL      IH      Imod   IL-Imod IH-Imod  |t|mod8  g  (empty?)\n");
    std::vector<i64> IL(1024,1),IH(1024,0);
    for(int t=-512;t<=511;t++){ if(!JOK[t+512]) continue; i64 dx=(i64)t<<13; i64 I1m=(arr(KC,dx)+K)>>23;
        if(I1m!=I1rule(t)) printf("RULE MISMATCH t=%d\n",t);
        i64 lo=0,hi=-1;bool any=false;
        for(i64 v=I1m-4000;v<=I1m+4000;v++){ i64 j=(arr(v,dx)+K)>>20; if(j>=JLo[t+512]&&j<=JHi[t+512]){ if(!any){lo=v;any=true;} hi=v; } }
        IL[t+512]=any?lo:1; IH[t+512]=any?hi:0;
        if(!any){ nempty++; printf("EMPTY t=%4d  JL=%lld JH=%lld  Imod=%lld  J(Imod)=%lld J(Imod-1)=%lld J(Imod+1)=%lld\n",
                    t,(long long)JLo[t+512],(long long)JHi[t+512],(long long)I1m,(long long)((arr(I1m,dx)+K)>>20),
                    (long long)((arr(I1m-1,dx)+K)>>20),(long long)((arr(I1m+1,dx)+K)>>20)); continue; }
        if(I1m>=lo&&I1m<=hi) nin++; }
    printf("empty=%d  model-in=%d\n",nempty,nin);
    // residual table for the t where the model I1 is OUTSIDE
    puts("\n# t where model I1 is outside the required interval");
    puts("# t     IL      IH      Imod   IL-Imod IH-Imod |t|%8");
    for(int t=-512;t<=511;t++){ if(!JOK[t+512]||IL[t+512]>IH[t+512]) continue; i64 I1m=I1rule(t);
        if(I1m>=IL[t+512]&&I1m<=IH[t+512]) continue;
        printf("%5d %8lld %8lld %8lld  %+5lld %+5lld   %d\n",t,(long long)IL[t+512],(long long)IH[t+512],(long long)I1m,
            (long long)(IL[t+512]-I1m),(long long)(IH[t+512]-I1m),((t%8)+8)%8); }
    // symmetry of the required I1: compare IL(-t) with -IH(t) etc.
    puts("\n# asymmetry of the required |I1|:  need |I1(-a)| in [ -IH(-a), -IL(-a) ]?  (I1(-a) is negative)");
    puts("# a   +a:[IL,IH]        -a:[IL,IH]      model+  model-");
    for(int a=1;a<=511;a+=1){ int ip=a+512,im=-a+512; if(IL[ip]>IH[ip]||IL[im]>IH[im])continue;
        if(a%64) continue;
        printf("%4d  [%8lld,%8lld] [%8lld,%8lld]  %8lld %8lld\n",a,(long long)IL[ip],(long long)IH[ip],(long long)IL[im],(long long)IH[im],(long long)I1rule(a),(long long)I1rule(-a)); }
    return 0; }
