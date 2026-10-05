// rt21_twoparam.cpp -- DIAGNOSTIC FIT per ROM word: square product = (arr(J, S_n + ds) + K)/2^u + phi , J = the chain.
// rt19: all violations at |t| < 198 need the product SMALLER, all 35 that need it LARGER are at |t| >= 198: an
// offset of one sign plus a term growing with |t| of the other.  ds is a change of the sine field (effect ~ t^2),
// phi a constant in 2^-26 units.  Looks for a pattern in (ds, phi) across words.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables();
    static i64 Jc[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; }
    puts("word  k  viol0 | best: ds   phi(units)  viol | phi-only  ds-only | ds as C0-units(2^-26): ds*2^(3-k)   C0-8S..");
    int t0=0,t1=0;
    for(int n=1;n<=16;n++){ const Tab&T=TB[n]; int k=T.u2-30; struct Smp{ i64 base,lo,hi,J; }; std::vector<Smp> S;
        for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; int flip=m>=17; i64 s=flip?-1:1;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} S.push_back({C0TAB[n]+s*T1-s*T3,lo,hi,Jc[t+512]}); } }
        auto viol=[&](int ds,int p512){ int v=0; i64 add=((i64)p512<<T.u2)/512; i64 Sv=T.S+ds; for(auto&x:S){ i64 T2=(arr(x.J,Sv)+K+add)>>T.u2; i64 V=x.base-T2; if(V<x.lo||V>x.hi)v++; } return v; };
        int v0=viol(0,0), best=v0,bd=0,bp=0, bpo=v0, bdo=v0;
        for(int ds=-24;ds<=24;ds++) for(int p=-40;p<=40;p++){ int v=viol(ds,p); if(v<best||(v==best&&abs(ds)+abs(p)<abs(bd)+abs(bp))){best=v;bd=ds;bp=p;} if(ds==0&&v<bpo)bpo=v; if(p==0&&v<bdo)bdo=v; }
        t0+=v0; t1+=best;
        printf(" %2d   %d   %3d  |      %+3d   %+8.4f    %3d  |   %3d      %3d   |   %+5d     (C0 - S*2^(3-k) = %+lld)\n",n,k,v0,bd,bp/512.0,best,bpo,bdo,bd*(1<<(3-k)),(long long)(C0TAB[n]-(T.S<<(3-k)))); }
    printf("total %d -> %d with (ds, phi) per word\n",t0,t1); return 0; }
