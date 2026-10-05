// rt9_append1.cpp -- "append a 1 below the truncated LSB" (JPH11-24893's U = upper bits with a 1 appended) applied to
// the square chain's latches.  For a floored signed latch this makes the magnitude symmetric in t:
//   I1' = 2*I1 + 1  (value I1 + 1/2) ;  J = reg-like (arr(I1', dx) + K) >> 21.
// The +-376 and +-420 events sit exactly at the mean of the model's two signs, which is what this predicts.
// Variants: a1 = append to I1, a2 = append to J (final shift +1), g = latch shift of I1 (3 = model), K2 on/off.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
static int score(const i64* J,int eu,int& guard){ int v=0; guard=0; for(int m=0;m<32;m++){ const Tab&T=TB[TABN[m]]; bool g=(m<=2||m==16||m>=30);
    for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; i64 T2=T.S? sm(arr(J[i],T.S)+K,T.u2+eu):0; i64 V=c.base-T2; if(V<c.lo||V>c.hi){v++; if(g)guard++;} } } return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    puts("a1 a2  g  K1 K2 | violations guard");
    for(int a1=0;a1<2;a1++)for(int a2=0;a2<2;a2++)for(int g=1;g<=4;g++)for(int k1=0;k1<2;k1++)for(int k2=0;k2<2;k2++){ static i64 J[1024]; int gd;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+(k1?K:0))>>(20+g); i64 rec=a1? 2*I1+1 : I1; int sh2=20+(a1?1:0)-(3-g);   // keep J at the coarse scale
            i64 Jv=(arr(rec,dx)+(k2?K:0))>>sh2; J[t+512]=a2? 2*Jv+1 : Jv; }
        int v=score(J,a2?1:0,gd); printf(" %d  %d  %d   %d  %d | %5d %5d%s\n",a1,a2,g,k1,k2,v,gd,(a1==0&&a2==0&&g==3&&k1&&k2)?"   <- the model":""); }
    return 0; }
