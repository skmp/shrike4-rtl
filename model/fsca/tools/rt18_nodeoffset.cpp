// rt18_nodeoffset.cpp -- DIAGNOSTIC FIT.  rt17 shows the residual is NODE-DEPENDENT (absent on words 1,2,16).  Give each
// ROM word n ONE constant phi_n (in 2^-26 units, steps of 1/256) added to its square product before truncation:
//   T2 = floor((arr(J,S_n) + K)/2^u + phi_n),   J = the chain, unchanged.
// A pure constant shifts every T2 boundary of that word by the same amount.  Report the best phi_n, the violations
// before/after, and how flat the optimum is (range of phi within +1 violation of the best).
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables();
    static i64 Jc[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; }
    puts("word   S        k  C0dev | viol(phi=0)  best phi (units)  viol   flat range of phi        | sin*2^26 frac   C0 low3  S low3");
    int tot0=0,tot1=0;
    for(int n=1;n<=16;n++){ const Tab&T=TB[n]; int nodes[2]={n,32-n}; int nn=n==16?1:2;
        struct Smp{ i64 base,lo,hi,P; }; std::vector<Smp> S;
        for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=m>=17; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;}
                S.push_back({C0TAB[n]+s*T1-s*T3,lo,hi,arr(Jc[t+512],T.S)+K}); } }
        auto viol=[&](int p256){ int v=0; i64 add=((i64)p256<<T.u2)/256; for(auto&x:S){ i64 T2=(x.P+add)>>T.u2; i64 V=x.base-T2; if(V<x.lo||V>x.hi)v++; } return v; };
        int v0=viol(0), best=v0, bp=0; for(int p=-512;p<=512;p++){ int v=viol(p); if(v<best||(v==best&&abs(p)<abs(bp))){best=v;bp=p;} }
        int lo=bp,hi=bp; while(lo>-512&&viol(lo-1)<=best+1)lo--; while(hi<512&&viol(hi+1)<=best+1)hi++;
        // fraction of the true sine at the 2^-26 grid
        i128 a=PI_()*n/32; i128 s=sin_(a); i128 v=s<<26; double fr=(double)((long double)(v%QONE)/(long double)QONE);
        tot0+=v0; tot1+=best;
        printf(" %2d  %06llx  %d   %+3lld  |   %3d         %+8.4f        %3d    [%+7.4f, %+7.4f]      |    %.3f         %lld       %lld\n",n,(long long)T.S,T.u2-30,(long long)(C0TAB[n]-T.sin26),v0,bp/256.0,best,lo/256.0,hi/256.0,fr,(long long)(C0TAB[n]&7),(long long)(T.S&7)); }
    printf("total: %d -> %d with one constant per ROM word\n",tot0,tot1); return 0; }
