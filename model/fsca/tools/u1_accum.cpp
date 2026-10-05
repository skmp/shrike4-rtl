// u1_accum.cpp -- THE FINAL ACCUMULATION, which the model assumes is an exact integer sum of four
// separately-truncated terms.  Exact integer addition is associative, so ORDER alone changes nothing;
// the testable hypotheses are about RETAINED FRACTION and TRUNCATION POINT.
//   If a term enters the adder with g extra bits and the SUM is truncated once, V shifts by sub-units
//   -- exactly the +-1 that is observed.
#include "base2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
static Fsca F;
static const int64_t C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
// which terms keep g extra bits into the accumulator; final truncation floor(0) or toward-zero(1)
static int score(int g,int keepT1,int keepT2,int keepT3,int mode){
    int viol=0;
    for(int a=0;a<32768;a++){
        int q=(int)((a+512u)>>10),m=q&63,t=a-1024*q,flip=0; if(t<-512)t+=65536;
        if(m>=17){m=32-m;flip=1;} if(m<0||m>16) continue;
        int64_t s=flip?-1:1, dx=(int64_t)t<<13; const Tab&T=TB[m];
        int gt1=keepT1?g:0, gt2=keepT2?g:0, gt3=keepT3?g:0;
        int64_t T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1-gt1):0;
        int64_t A3=((arr(((int64_t)t*t)<<3,dx)+K)>>20)>>3;
        int64_t T3=T.c3? sm(arr(T.c3<<5,A3)+K,26-gt3):0;
        int64_t I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
        int64_t T2=T.S? sm(arr(J,T.S)+K,T.u2-gt2):0;
        // accumulate at g extra bits: scale the already-integer parts up, subtract the fine parts
        int64_t acc = (C0T[m]<<g) + s*(T1<<(g-gt1)) - (T2<<(g-gt2)) - s*(T3<<(g-gt3));
        int64_t V = mode? sm(acc,g) : (acc>>g);
        Win w=decode(F.srz[a],F.srn[a]); int64_t lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        if(V<lo||V>hi) viol++;
    }
    return viol;
}
int main(){ if(!load_fsca(F)) return 1; build_tables();
    printf("FSCA final accumulation: V = C0 + s*T1 - T2 - s*T3\n");
    printf("The model truncates every term separately then adds exactly. Test RETAINING g extra bits on\n");
    printf("selected terms and truncating the SUM once. g=0 reproduces the model.\n\n");
    printf("  baseline (g=0) = %d violations of 32768 (expect 147)\n\n",score(0,0,0,0,0));
    printf("  g  keep(T1,T2,T3)  floor   toward-zero\n");
    int best=1<<30; char bs[64]="";
    for(int g=1;g<=6;g++)
      for(int k=1;k<8;k++){
        int k1=(k>>0)&1,k2=(k>>1)&1,k3=(k>>2)&1;
        int f=score(g,k1,k2,k3,0), z=score(g,k1,k2,k3,1);
        printf("  %d   (%d,%d,%d)          %5d   %5d%s\n",g,k1,k2,k3,f,z,
            (f<147||z<147)?"   <== beats 147":"");
        int b=f<z?f:z; if(b<best){best=b; snprintf(bs,64,"g=%d keep(%d,%d,%d) %s",g,k1,k2,k3,f<z?"floor":"toward-zero");}
      }
    printf("\n  BEST %d violations at %s   (baseline 147)\n",best,bs);
    return 0; }
