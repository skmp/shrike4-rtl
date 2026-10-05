// e2_signconv.cpp -- DIRECTION 2, step 2: the array's NEGATIVE-RECODED-OPERAND behaviour is validated nowhere.
// FIPR's datapath is sign-magnitude and its Booth recoder only ever sees a normalised positive mantissa, so
// "recode the two's-complement negative value with sign extension" is an extrapolation invented by the model.
// The patent's arithmetic block is sign-magnitude, so the physically natural rule is that the recoder sees the
// MAGNITUDE and the sign is applied outside.  Those differ, and they differ only for t < 0 -- exactly the half
// where the square path's asymmetry lives.
// Nodes 0 and 32 (sin = 0, so no square term at all) are unaffected by this choice, so they remain a clean
// guard on the negative-MULTIPLICAND behaviour, which they do validate.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// conv: how the array treats a negative recoded operand (and, for 4..6, a negative multiplicand too)
static inline i64 arrX(i64 rec,i64 mul,int conv){
    switch(conv){
      case 0: return arr(rec,mul);                                             // current: two's-complement rec
      case 1: return rec<0 ? -arr(-rec,mul) : arr(rec,mul);                    // sign-magnitude rec, negate after
      case 2: return rec<0 ? ~arr(-rec,mul) : arr(rec,mul);                    // sign-magnitude rec, one's complement
      case 3: { i64 s=(rec<0)?-1:1; return s*arr(rec<0?-rec:rec,mul); }        // same as 1, written out
      case 4: { int sr=rec<0, sm2=mul<0; i64 p=arr(rec<0?-rec:rec, mul<0?-mul:mul);
                return (sr^sm2)? -p : p; }                                     // sign-magnitude on BOTH operands
      case 5: { int sr=rec<0, sm2=mul<0; i64 p=arr(rec<0?-rec:rec, mul<0?-mul:mul);
                return (sr^sm2)? ~p : p; }                                     // both magnitudes, one's-complement result
      default:{ int sr=rec<0, sm2=mul<0; i64 p=arr(mul<0?-mul:mul, rec<0?-rec:rec);
                return (sr^sm2)? -p : p; } }                                   // both magnitudes, roles swapped
}
static const char* cn[7]={"two's-complement rec (current)","sign-magnitude rec, negate after",
    "sign-magnitude rec, one's complement","(= 1)","BOTH operands by magnitude","both magnitudes, 1's-comp result",
    "both magnitudes, roles swapped"};
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts("convention applied to the square chain's multiplies (the only negative-rec uses in the model)");
    puts("first multiply | conv | guard(0,1,2,16) | total violations over nodes 0..31   [baseline 147/148]");
    int best=1<<30; int bf=0,bc=0;
    for(int firstform=0;firstform<2;firstform++)            // 0: arr(KC,dx)   1: arr(dx,KC)
      for(int conv=0;conv<7;conv++){ if(conv==3) continue;
        int viol=0,guard=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 P1 = firstform? arrX(dx,KC,conv) : arrX(KC,dx,conv);
                i64 I1=(P1+K)>>23;
                i64 J=(arrX(I1,dx,conv)+K)>>20;
                i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]):0;
                i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi){ viol++; if(n==0||n==1||n==2||n==16) guard++; } } }
        printf("  %-11s |  %d   |      %3d        | %4d%s\n", firstform?"arr(dx,KC)":"arr(KC,dx)",conv,guard,viol,
            (guard==0&&viol<147)?"   <== better AND guard-clean":(viol<147?"   (better, guard broken)":""));
        if(guard==0&&viol<best){ best=viol; bf=firstform; bc=conv; } }
    printf("\nbest guard-clean: %d violations, first multiply %s, convention %d = %s\n",
        best,bf?"arr(dx,KC)":"arr(KC,dx)",bc,cn[bc]);
    return 0; }
