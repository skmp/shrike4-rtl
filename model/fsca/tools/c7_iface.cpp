// c7_iface.cpp -- the I1 -> I2 interface, which the joint programme just identified as the unique unexplained
// spot (the square path is the only place in either instruction where one truncating latch feeds another).
// The patent's arithmetic block is SIGN-MAGNITUDE, so a latch may round the MAGNITUDE rather than the
// two's-complement value -- for a negative product those differ, and the chain latch convention was never tested.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// latch conventions for a signed array output P with rounding constant Kc at shift s
static inline i64 latch(i64 P,i64 Kc,int s,int c){ i64 m,r;
  switch(c){
    case 0: return (P+Kc)>>s;                                      // two's-complement floor (current)
    case 1: return P>=0? ((P+Kc)>>s) : -(((-P)+Kc-1)>>s);          // toward zero on the value
    case 2: return P>=0? ((P+Kc)>>s) : -((~(P+Kc))>>s);            // one's-complement magnitude
    case 3: m = P<0?-P:P; r=(m+Kc)>>s; return P<0? -r : r;         // SIGN-MAGNITUDE: round the magnitude  <-- new
    case 4: m = P<0?-P:P; r=(m>=Kc)?((m-Kc)>>s):0; return P<0? -r : r;   // magnitude minus the constant
    default: m = P<0?-P:P; r=m>>s; return P<0? -r : r; }           // magnitude, plain truncation
}
static const char* cn[6]={"two's-comp floor (current)","toward zero","one's-comp magnitude",
                          "SIGN-MAGNITUDE +K","magnitude -K","magnitude truncate"};
int main(){ if(!load_fsca(F))return 1; build_tables();
    printf("latch conventions for the I1 -> I2 interface (window violations over nodes 0..31; baseline 147)\n");
    printf("%-28s","I1 conv \\ I2 conv"); for(int j=0;j<6;j++) printf("%9d",j); printf("\n");
    int gbest=1<<30; int bi=0,bj=0,bs1=0,bs2=0;
    for(int c1=0;c1<6;c1++){ printf("%-28s",cn[c1]);
      for(int c2=0;c2<6;c2++){ int best=1<<30,bb1=0,bb2=0;
        for(int s1=21;s1<=26;s1++) for(int s2=43-s1-2;s2<=43-s1+2;s2++){ if(s2<14||s2>26) continue;
          int viol=0;
          for(int node=0;node<=31&&viol<best;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
              i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
              i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
              i64 I1=latch(arr(KC,dx),K,s1,c1);
              i64 J =latch(arr(I1,dx),K,s2,c2);
              int du=20-s2; i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]+du):0;
              i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
              if(V<lo||V>hi) viol++; } }
          if(viol<best){ best=viol; bb1=s1; bb2=s2; } }
        printf("%9d",best);
        if(best<gbest){ gbest=best; bi=c1; bj=c2; bs1=bb1; bs2=bb2; } }
      printf("\n"); }
    printf("\nbest: I1 = %s, I2 = %s, shifts (%d, %d) -> %d violations\n",cn[bi],cn[bj],bs1,bs2,gbest);
    return 0; }
