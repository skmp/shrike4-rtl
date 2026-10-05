// b2.cpp -- joint fit of the shared table entries (C0, u2) over the two nodes that use them, then per-node score.
// Fundamental domain: nodes 0..31, t in [-512,511]  (nodes 32..63 are the exact half-turn negation).
#include "base2.h"
#include <cstring>
static Fsca F;
static inline void win_of(int a, i64& lo, i64& hi, bool& ex){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} ex=w.exact; }
// V for table n, flip = cos sign flipped (nodes 17..31), given C0/u2 overrides
static inline i64 Vof(int n,int t,int flip,i64 C0,int u2){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1=(arr(KC,dx)+K)>>23, I2=(arr(I1,dx)+K)>>20; i64 T2=T.S? sm(arr(I2,T.S)+K,u2):0;
    i64 s=flip?-1:1; return C0+s*T1-T2-s*T3; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    i64 bestC0[17]; int bestU[17];
    puts("tab  C0        (RN26 delta)  u2   viol(node n)  viol(node 32-n)  total");
    int grand=0;
    for(int n=0;n<=16;n++){
        int nodes[2]={n, 32-n}; int nn = (n==0||n==16)?1:2;   // node 32 belongs to the other half; node 16 is alone
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int bv=1<<30; i64 bc=lo0; int bu=TB[n].u2; int bper[2]={0,0};
        for(i64 c=lo0-3;c<=hi0+3;c++) for(int u=TB[n].u2-1;u<=TB[n].u2+1;u++){
            int v=0,per[2]={0,0};
            for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17); int tn=flip?32-m:m;
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    i64 V=Vof(tn,t,flip,c,u); if(V<lo||V>hi){v++;per[i]++;} } }
            if(v<bv){bv=v;bc=c;bu=u;bper[0]=per[0];bper[1]=per[1];} }
        bestC0[n]=bc; bestU[n]=bu; grand+=bv;
        printf("%2d  %9lld  %+4lld       %2d   %5d          %5d        %5d%s\n",n,(long long)bc,
            (long long)(bc-TB[n].sin26),bu,bper[0],bper[1],bv, bu!=TB[n].u2?"  <-- u2 moved":"");
    }
    printf("grand total window violations over nodes 0..31: %d\n",grand);
    printf("\nstatic const i64 C0TAB[17] = {"); for(int n=0;n<=16;n++)printf("%lld%s",(long long)bestC0[n],n<16?",":"};\n");
    printf("static const int U2TAB[17] = {"); for(int n=0;n<=16;n++)printf("%d%s",bestU[n],n<16?",":"};\n");
    return 0; }
