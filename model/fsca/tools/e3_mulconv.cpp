// e3_mulconv.cpp -- DIRECTION 2, step 3.  FIPR never presents a negative MULTIPLICAND either; the model's rule
// (form |d|*mul in two's complement, then one's-complement the row when the Booth digit is negative) is another
// extrapolation.  A two's-complement array with a signed multiplicand would more naturally make each row
// +|d|*|mul| or its one's complement according to the XOR of the two signs, with the hot one following the ROW's
// sign rather than the digit's.  All such variants agree with the current model whenever mul >= 0, so they
// preserve FIPR by construction.  Nodes 0 and 32 (sin = 0, so no square term) exercise the negative multiplicand
// through the linear and cubic terms for all 1024 offsets: that is the test of whether they PIN the rule.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 arrM(i64 rec,i64 mul,int conv){ i64 R[14]; i64 T=0;
    int negm = mul<0; i64 amul = negm? -mul : mul;
    for(int k=0;k<14;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag,row; int rowneg;
        switch(conv){
          case 0: mag=(i64)(d<0?-d:d)*mul; row=(d<0)?~mag:mag; rowneg=(d<0); break;      // current
          case 1: mag=(i64)(d<0?-d:d)*amul; rowneg=((d<0)^negm)&&d!=0; row=rowneg?~mag:mag; break;
          case 2: mag=(i64)(d<0?-d:d)*amul; rowneg=((d<0)^negm)&&d!=0; row=rowneg?-mag:mag; break;
          default:mag=(i64)(d<0?-d:d)*mul; row=(d<0)?-mag:mag; rowneg=(d<0); break; }
        R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16;
        if(rowneg && 2*k>=18) T += 1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T += (1ll<<16)*(m15>=2);
    int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T -= (1ll<<16)*p16; return T; }
static const char* cn[4]={"current: |d|*mul in two's complement, row negated by the DIGIT's sign",
                          "rows +-|d|*|mul|, one's complement by the XOR of both signs",
                          "rows +-|d|*|mul|, true negation by the XOR of both signs",
                          "|d|*mul, true negation by the digit's sign"};
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts("conv | node 0+32 misses (2048 samples, the guard that PINS the rule) | full score | FIPR preserved?");
    for(int conv=0;conv<4;conv++){
        int g=0,viol=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arrM(TB[n].C1f,dx,conv)+K,23-TB[n].e1):0;
                i64 A3=((arrM(((i64)t*t)<<3,dx,conv)+K)>>20)>>3;
                i64 T3=TB[n].c3? sm(arrM(TB[n].c3<<5,A3,conv)+K,26):0;
                i64 I1=(arrM(dx,KC,conv)+K)>>23; i64 J=(arrM(I1,dx,conv)+K)>>20;
                i64 T2=TB[n].S? sm(arrM(J,TB[n].S,conv)+K,U2TAB[n]):0;
                i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi){ viol++; if(n==0) g++; } } }
        // FIPR preservation: all variants agree with conv 0 when mul >= 0
        int diff=0; for(i64 r=(1ll<<23); r<(1ll<<24); r+= 7919) for(i64 m=(1ll<<23); m<(1ll<<24); m+= 104729)
            if(arrM(r,m,conv)!=arrM(r,m,0)) diff++;
        printf("  %d  |   %4d   | %4d | %s\n",conv,g,viol,diff?"NO - changes FIPR too":"yes (identical for mul >= 0)");
        printf("       %s\n",cn[conv]); }
    return 0; }
