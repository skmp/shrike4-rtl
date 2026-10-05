// rt32_perword.cpp -- at each small offset where the chain violates some word, what does EACH word allow for J?
// For every ROM word: the interval of integer J (coarse) its two nodes accept, as an offset from the chain's J.
// Only the words whose interval edge lies within 12 units of the chain are shown (the binding ones).
#include "base2.h"
#include <string>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables(); int TM=argc>1?atoi(argv[1]):70;
    for(int t=1;t<=TM;t++) for(int sg=1;sg>=-1;sg-=2){ int tt=sg*t; i64 dx=(i64)tt<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 Jc=(arr(I1,dx)+K)>>20;
        struct W{int n; i64 lo,hi;}; std::vector<W> ws; bool viol=false; i64 gl=-(1ll<<60),gh=(1ll<<60);
        for(int n=1;n<=16;n++){ const Tab&T=TB[n]; i64 lo=-(1ll<<60),hi=(1ll<<60);
            for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; int flip=m>=17; i64 s=flip?-1:1; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)tt*tt)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                int a=(m*1024+tt)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 vlo,vhi; if(w.sign>0){vlo=w.lo;vhi=w.hi;} else {vlo=-w.hi;vhi=-w.lo;} i64 base=C0TAB[n]+s*T1-s*T3;
                i64 jl=-1,jh=-1; for(i64 j=(Jc>600?Jc-600:0);j<=Jc+600;j++){ i64 V=base-sm(arr(j,T.S)+K,T.u2); if(V>=vlo&&V<=vhi){ if(jl<0)jl=j; jh=j; } }
                if(jl<0){ jl=1; jh=0; } if(jl>lo)lo=jl; if(jh<hi)hi=jh; }
            if(Jc<lo||Jc>hi)viol=true; ws.push_back({n,lo,hi}); if(lo>gl)gl=lo; if(hi<gh)gh=hi; }
        if(!viol)continue;
        printf("t=%4d chain J=%7lld common window [%+lld,%+lld]%s | binding words:",tt,(long long)Jc,(long long)(gl-Jc),(long long)(gh-Jc),gl>gh?" EMPTY":"");
        for(auto&w:ws){ bool lb = (w.lo-Jc)>=-12, hb=(w.hi-Jc)<=12; if(lb||hb) printf("  w%d[%s,%s]",w.n, lb?std::to_string(w.lo-Jc).c_str():"..", hb?std::to_string(w.hi-Jc).c_str():".."); } puts(""); }
    return 0; }
