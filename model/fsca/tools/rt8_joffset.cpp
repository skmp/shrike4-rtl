// rt8_joffset.cpp -- full-model landscape of a uniform offset c on the square latch (fine scale: T2 = sm(arr(J18+c,S)+K,u2+2)),
// and of the same offset applied only for t>=0 / only for t<0.  Baseline (coarse J) 148; fine-scale baseline printed at c=0.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
static int score(const i64* J,int& guard){ int v=0; guard=0; for(int m=0;m<32;m++){ const Tab&T=TB[TABN[m]]; bool g=(m<=2||m==16||m>=30);
    for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; i64 T2=T.S? sm(arr(J[i],T.S)+K,T.u2+2):0; i64 V=c.base-T2; if(V<c.lo||V>c.hi){v++; if(g)guard++;} } } return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    static i64 J0[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J0[t+512]=(arr(I1,dx)+K)>>18; }
    puts("   c(fine)   both sides        t>=0 only         t<0 only      (violations / guard)");
    for(int c=-24;c<=12;c+=2){ static i64 Ja[1024],Jp[1024],Jn[1024]; for(int i=0;i<1024;i++){ int t=i-512; Ja[i]=J0[i]+c; Jp[i]=J0[i]+(t>=0?c:0); Jn[i]=J0[i]+(t<0?c:0); if(Ja[i]<0)Ja[i]=0; if(Jp[i]<0)Jp[i]=0; if(Jn[i]<0)Jn[i]=0; }
        int g1,g2,g3; int v1=score(Ja,g1),v2=score(Jp,g2),v3=score(Jn,g3); printf("  %+4d      %4d / %-3d        %4d / %-3d        %4d / %-3d\n",c,v1,g1,v2,g2,v3,g3); }
    return 0; }
