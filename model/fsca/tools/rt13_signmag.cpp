// rt13_signmag.cpp -- does the data REQUIRE a signed multiplicand, or would a FIPR-style sign-magnitude multiply
// (array sees |dx|, sign applied at the aligner) do?   b25.cpp only varied the truncation of a SIGNED product.
// (1) model-free: at node 0 (sin = 0 -> no even part, all values exact) a sign-magnitude odd part would give
//     sin(-t) == -sin(t) bit-for-bit.
// (2) sign-magnitude variants of the linear + cubic terms on nodes 0 and 32 (2048 exact samples).
#include "base2.h"
static Fsca F;
static inline i64 Vhw(int a,bool&ex){ Win w=decode(F.srz[a&0xffff],F.srn[a&0xffff]); ex=w.exact; return w.sign*w.lo; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    int asym=0, n=0, notexact=0; int first[8]; int nf=0;
    for(int t=1;t<=511;t++){ bool e1,e2; i64 vp=Vhw(t,e1), vn=Vhw(65536-t,e2); if(!e1||!e2){notexact++;continue;} n++; if(vp!=-vn){ asym++; if(nf<8)first[nf++]=t; } }
    printf("(1) node 0, model-free: sin(-t) != -sin(t) at %d of %d exact pairs",asym,n); if(nf){printf("  e.g. t ="); for(int i=0;i<nf;i++)printf(" %d",first[i]);} puts("");
    // how different: distribution of V(+t) + V(-t)
    { int hist[5]={0}; for(int t=1;t<=511;t++){ bool e1,e2; i64 d=Vhw(t,e1)+Vhw(65536-t,e2); if(d>=-2&&d<=2)hist[d+2]++; } printf("    V(+t)+V(-t): -2:%d -1:%d 0:%d +1:%d +2:%d\n",hist[0],hist[1],hist[2],hist[3],hist[4]); }
    // (2) variants.  mag products from |dx|; term = -(...) for t<0.
    const Tab&T=TB[0];
    const char* nm[]={"signed model (arr(C,dx), sm)","sign-mag, -(X>>u) exact negate (FIPR aligner)","sign-mag, ~(X>>u) no hot one","sign-mag, K subtracted: -((X-2K)>>u)","sign-mag, -((X-2K-1)>>u)"};
    for(int var=0;var<5;var++){ int miss[2]={0,0};
        for(int node=0;node<2;node++) for(int t=-512;t<=511;t++){ int a=((node?32:0)*1024+t)&0xffff; bool ex; i64 hw=Vhw(a,ex); if(!ex)continue; i64 s=node?-1:1; i64 V;
            if(var==0){ i64 dx=(i64)t<<13; i64 T1=sm(arr(T.C1f,dx)+K,23-T.e1); i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=sm(arr(T.c3<<5,A3)+K,26); V=s*T1-s*T3; }
            else { i64 am=t<0?-t:t; i64 dxm=am<<13; i64 X1=arr(T.C1f,dxm)+K; i64 A3m=((arr((am*am)<<3,dxm)+K)>>20)>>3; i64 X3=arr(T.c3<<5,A3m)+K; i64 m1,m3; int u1=23-T.e1;
                 if(t>=0||var==1){ m1=X1>>u1; m3=X3>>26; }
                 else if(var==2){ m1=(X1>>u1)+1; m3=(X3>>26)+1; }        // ~x = -x-1 -> magnitude x+1
                 else if(var==3){ m1=(X1-2*K)>>u1; m3=(X3-2*K)>>26; }
                 else { m1=(X1-2*K-1)>>u1; m3=(X3-2*K-1)>>26; }
                 i64 sg=(t<0?-1:1)*s; V=sg*(m1-m3); }
            if(V!=hw)miss[node]++; }
        printf("(2) %-48s node 0 miss %4d   node 32 miss %4d\n",nm[var],miss[0],miss[1]); }
    return 0; }
