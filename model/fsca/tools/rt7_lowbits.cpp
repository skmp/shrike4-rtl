// rt7_lowbits.cpp -- RTL-motivated diagnostic: are the LOW BITS of the fed-back square latch J something other
// than J's own?  (truncated / forced / stale contents of the port-B latch from the previous step, i.e. I1 or the
// raw KC*dx register, which is what that latch held one cycle earlier in rtl/fpu_vec's FSCA microprogram.)
// Full model, nodes 0..31, guard nodes reported.  Baseline 148.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
static int score(const i64* J,int& guard){ int v=0; guard=0; for(int m=0;m<32;m++){ const Tab&T=TB[TABN[m]]; bool g=(m<=2||m==16||m>=30);
    for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; i64 T2=T.S? sm(arr(J[i],T.S)+K,T.u2):0; i64 V=c.base-T2; if(V<c.lo||V>c.hi){v++; if(g)guard++;} } } return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    static i64 J0[1024],I1v[1024],M2v[1024],DXv[1024],Q0[1024],A3v[1024];
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 M2=(arr(KC,dx)+K)>>20; i64 I1=M2>>3; J0[t+512]=(arr(I1,dx)+K)>>20; I1v[t+512]=I1; M2v[t+512]=M2; DXv[t+512]=dx>>13; Q0[t+512]=(arr(dx,dx)+K)>>20; A3v[t+512]=(arr(((i64)t*t)<<3,dx)+K)>>20; }
    int g; { int v=score(J0,g); printf("baseline                         : %4d (guard %d)\n",v,g); }
    const char* srcn[]={"zero (truncate)","ones (force)","I1","KC*dx reg","t","|I1|","64t^2 reg","t^3 reg"};
    for(int src=0;src<8;src++) for(int m=1;m<=6;m++){ i64 mask=(1ll<<m)-1; static i64 J[1024];
        for(int i=0;i<1024;i++){ i64 x=0; switch(src){case 0:x=0;break;case 1:x=mask;break;case 2:x=I1v[i];break;case 3:x=M2v[i];break;case 4:x=DXv[i];break;case 5:x=llabs(I1v[i]);break;case 6:x=Q0[i];break;case 7:x=A3v[i];break;} J[i]=(J0[i]&~mask)|(x&mask); }
        int v=score(J,g); printf("J low %d bits <- %-16s : %4d (guard %d)\n",m,srcn[src],v,g); }
    return 0; }
