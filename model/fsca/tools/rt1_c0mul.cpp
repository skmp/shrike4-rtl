// rt1_c0mul.cpp -- RTL-driven hypothesis H1: the square term's sine operand is the table's 26-bit C0 itself
// (one ROM entry feeding both the adder and the multiplicand port), not a 23-bit normalised field.
//   baseline : T2 = sm(arr(J, S23) + K, 30 + k)            (base2.h)
//   H1a      : T2 = sm(arr(J, C0)  + K, 33)                 C0 as 26-bit multiplicand, fixed alignment
//   H1b      : T2 = sm(arr(C0, J)  + K, 33)                 C0 recoded (27-bit recoder), J multiplicand
//   H1c      : T2 = sm(arr(J, C0>>(3-k)) + K, 30+k)         26-bit table truncated to the 23-bit port (no RN)
// Scores: window violations over nodes 0..31 (baseline 147) and guard nodes 0/1/2/16 per node.
#include "base2.h"
static Fsca F;
static inline void win_of(int a, i64& lo, i64& hi){ Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 V_variant(int var,int n,int t,int flip){ const Tab&T=TB[n]; i64 dx=(i64)t<<13; i64 C0=C0TAB[n];
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20; i64 T2=0;
    int k=T.u2-30;
    switch(var){
      case 0: T2=T.S? sm(arr(J,T.S)+K,T.u2):0; break;
      case 1: T2=C0? sm(arr(J,C0)+K,33):0; break;
      case 2: T2=C0? sm(arr(C0,J)+K,33):0; break;
      case 3: T2=C0? sm(arr(J,C0>>(3-k))+K,T.u2):0; break;
      case 4: T2=C0? sm(arr(J,C0>>2)+K,32):0; break;           // 24-bit port, truncated
      case 5: T2=C0? sm(arr(J,C0>>3)+K,31):0; break;           // 23-bit port, truncated, no normalisation
    }
    i64 s=flip?-1:1; return C0+s*T1-T2-s*T3; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    const char* names[]={"baseline S23 RN","H1a C0(26) mul u=33","H1b C0(26) rec u=33","H1c C0>>(3-k) trunc","C0>>2 (24b) u=32","C0>>3 (23b) u=31"};
    for(int var=0;var<6;var++){ int tot=0; int per[32]={0};
        for(int m=0;m<32;m++){ int flip=m>=17; int tn=flip?32-m:m;
            for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi); i64 V=V_variant(var,tn,t,flip); if(V<lo||V>hi){tot++;per[m]++;} } }
        printf("%-24s total %4d | guard n0 %d n1 %d n2 %d n16 %d n30 %d n31 %d | per node:",names[var],tot,per[0],per[1],per[2],per[16],per[30],per[31]);
        for(int m=0;m<32;m++)printf(" %d",per[m]); puts(""); }
    return 0; }
