// rt31_smallt.cpp -- the small-|t| regime, where everything is simple: the chain's first-latch term is < 1 unit, there
// is no asymmetry to speak of, and the hardware's square product is only ever BELOW the model's (rt22).
// Candidates for the hardware's J as a function of X = KC t^2 / 2^17 (real), scored on the FULL model restricted to
// |t| <= T:   J = floor(X - c)  for c in [-0.5, 5] ;   J = floor(X) & ~(2^g - 1)  (low bits dropped) ;  J = floor(X/2^g - c')*2^g.
#include "base2.h"
#include <cmath>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
static int score(const i64* J,int T,int* guard=0){ int v=0,g=0; for(int m=1;m<32;m++){ const Tab&Tb=TB[TABN[m]]; bool gd=(m<=2||m==16||m>=30); for(int t=-T;t<=T;t++){ if(!t)continue; const Cell&c=CELL[m][t+512]; i64 T2=Tb.S? sm(arr(J[t+512],Tb.S)+K,Tb.u2):0; i64 V=c.base-T2; if(V<c.lo||V>c.hi){v++; if(gd)g++;} } } if(guard)*guard=g; return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    static i64 J[1024]; int Ts[]={63,127,199}; 
    for(int T:Ts){ printf("\n|t| <= %d  (%d samples)\n",T,31*2*T);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J[t+512]=(arr(I1,dx)+K)>>20; } int g; int v=score(J,T,&g); printf("   the chain                      : %3d violations (guard %d)\n",v,g);
        int best=1<<30; double bc=0; printf("   J = floor(X - c):   c  -> violations\n      ");
        for(int q=-10;q<=100;q++){ double c=q*0.05; for(int t=-512;t<=511;t++){ long double X=(long double)KC*t*t/131072.0L; i64 j=(i64)floorl(X-c); if(j<0)j=0; J[t+512]=j; } int vv=score(J,T,&g); if(vv<best){best=vv;bc=c;} if(q%5==0) printf("%.2f:%d(%d) ",c,vv,g); }
        printf("\n      best c = %.2f -> %d\n",bc,best);
        for(int gb=1;gb<=3;gb++){ int bb=1<<30; double bcc=0; for(int q=-20;q<=40;q++){ double c=q*0.05; for(int t=-512;t<=511;t++){ long double X=(long double)KC*t*t/131072.0L; i64 j=(i64)floorl(X/(1<<gb)-c); if(j<0)j=0; J[t+512]=j<<gb; } int vv=score(J,T,&g); if(vv<bb){bb=vv;bcc=c;} } printf("   J = floor(X/%d - c)*%d          : best %3d at c = %.2f\n",1<<gb,1<<gb,bb,bcc); } }
    return 0; }
