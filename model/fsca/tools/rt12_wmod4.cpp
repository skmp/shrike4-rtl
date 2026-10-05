// rt12_wmod4.cpp -- DIAGNOSTIC (not a model): rescale this model's own sign asymmetry by a weight that depends on
// t mod 4.  J_sym = mean of the model's fine J at +-t ; J(+-t) = J_sym -+ w(t mod 4) * D_model/2, D_model = J(-t)-J(+t).
// The model is w = {1,1,1,1}.  The event data suggested w ~ {0..1/4, 1, 2, 1..1.5}.  Full model, fine scale.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024]; static int TABN[32];
static int score(const i64* J,int& guard,int* pernode=0){ int v=0; guard=0; for(int m=0;m<32;m++){ const Tab&T=TB[TABN[m]]; bool g=(m<=2||m==16||m>=30); int pn=0;
    for(int i=0;i<1024;i++){ const Cell&c=CELL[m][i]; i64 T2=T.S? sm(arr(J[i],T.S)+K,T.u2+2):0; i64 V=c.base-T2; if(V<c.lo||V>c.hi){v++;pn++; if(g)guard++;} } if(pernode)pernode[m]=pn; } return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; TABN[m]=n; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    static i64 J0[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J0[t+512]=(arr(I1,dx)+K)>>18; }
    int g; int v0=score(J0,g); printf("fine-scale model (w=1,1,1,1): %d violations, guard %d\n",v0,g);
    // weights in eighths
    int W0[]={0,1,2,4,8}, W1[]={4,8,12}, W2[]={8,12,16,20,24}, W3[]={4,8,12,16};
    puts(" w0   w1   w2   w3  | violations guard   (weights in units of 1/8; model = 8 8 8 8)");
    int best=1<<30; int bw[4]={8,8,8,8};
    for(int a:W0)for(int b:W1)for(int c:W2)for(int d:W3){ int w8[4]={a,b,c,d}; static i64 J[1024]; J[0]=J0[0];
        for(int t=1;t<512;t++){ i64 jp=J0[512+t], jn=J0[512-t]; i64 D=jn-jp; int w=w8[t&3]; // keep the mean, rescale the difference
            i64 sum=jp+jn; i64 Dn=(D*w+ (D>=0?4:-4))/8; i64 np=(sum-Dn)/2, nn=np+Dn; J[512+t]=np; J[512-t]=nn; }
        J[512]=J0[512];
        int gg; int v=score(J,gg); if(v<best||(v==best)){ } 
        if(v<=v0-10 || (a==8&&b==8&&c==8&&d==8)) printf(" %2d   %2d   %2d   %2d  | %5d %5d\n",a,b,c,d,v,gg);
        if(v<best){best=v;bw[0]=a;bw[1]=b;bw[2]=c;bw[3]=d;} }
    printf("best: w = %d %d %d %d (/8) -> %d violations\n",bw[0],bw[1],bw[2],bw[3],best);
    return 0; }
