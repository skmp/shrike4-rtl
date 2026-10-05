// fv_final-fields_2_chain.cpp -- INDEPENDENT check of "the guards pin the chain to (23,20,K2=2^19)".
// Sweeps all 2280 chains  I1=((arr(dx,KC)|arr(KC,dx))+K1)>>s1 ; J=(arr(I1,dx)+K2)>>s2
// with s1 in [16,30], s2 in [10,28], K1,K2 in {0,2^19}, both operand orders, BASELINE final square product,
// coefficient tables frozen.  Reports every chain that keeps nodes 0,1,2,16,30,31 exact for all 1024 offsets,
// plus the full nodes-0..31 score of each survivor, and the best score overall (guard-breaking or not).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 WLO[65536],WHI[65536];
static inline i64 Vof(int n,int t,int flip,int ro,int s1,int s2,i64 K1,i64 K2,int sgn){
    const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K1)>>s1, J=(arr(I1,dx)+K2)>>s2;
    i64 T2=0; if(T.S){ i64 P=arr(J,T.S)+K; T2 = sgn? ((-(-((~(-P))>>T.u2)))) : sm(P,T.u2); }
    i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3; }
static int guardviol(int ro,int s1,int s2,i64 K1,i64 K2,int sgn){
    const int gn[6]={0,1,2,16,30,31}; int v=0;
    for(int i=0;i<6;i++){ int node=gn[i],flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 V=Vof(n,t,flip,ro,s1,s2,K1,K2,sgn);
            if(V<WLO[a]||V>WHI[a])v++; } }
    return v; }
static int fullviol(int ro,int s1,int s2,i64 K1,i64 K2,int sgn){ int v=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 V=Vof(n,t,flip,ro,s1,s2,K1,K2,sgn);
            if(V<WLO[a]||V>WHI[a])v++; } }
    return v; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){WLO[a]=w.lo;WHI[a]=w.hi;} else {WLO[a]=-w.hi;WHI[a]=-w.lo;} }
    int sgn = argc>1? atoi(argv[1]) : 0;
    printf("final square product: %s\n\n", sgn? "one's-complement (P+K-1)>>u" : "published sm(P+K,u)");
    int nsurv=0,ntot=0,best=1<<30,bs1=0,bs2=0,bro=0; i64 bK1=0,bK2=0; int bg=0;
    for(int ro=0;ro<2;ro++) for(int s1=16;s1<=30;s1++) for(int s2=10;s2<=28;s2++)
      for(int i1=0;i1<2;i1++) for(int i2=0;i2<2;i2++){
        i64 K1=i1?(1ll<<19):0, K2=i2?(1ll<<19):0; ntot++;
        int g=guardviol(ro,s1,s2,K1,K2,sgn);
        if(g==0){ int f=fullviol(ro,s1,s2,K1,K2,sgn); nsurv++;
            printf("GUARD-CLEAN  s1=%2d s2=%2d %-12s K1=%-7lld K2=%-7lld  full=%d\n",
                s1,s2,ro?"arr(dx,KC)":"arr(KC,dx)",(long long)K1,(long long)K2,f); }
        int f=fullviol(ro,s1,s2,K1,K2,sgn);
        if(f<best){best=f;bs1=s1;bs2=s2;bro=ro;bK1=K1;bK2=K2;bg=g;}
      }
    printf("\nchains swept: %d   guard-clean: %d\n",ntot,nsurv);
    printf("best chain overall (guards ignored): s1=%d s2=%d %s K1=%lld K2=%lld -> full=%d, guard violations=%d\n",
        bs1,bs2,bro?"arr(dx,KC)":"arr(KC,dx)",(long long)bK1,(long long)bK2,best,bg);
    return 0; }
