// b25.cpp -- nodes 0 and 32 have sin = 0, so V = +-T1 -+ T3 with NO square term: 2048 samples that pin the
// truncation conventions of the linear and cubic terms on their own.  Enumerate the conventions, keep those
// that are exact there, and for each, report the floor achievable with a SYMMETRIC square latch.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <array>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// truncation of a signed product to a magnitude at unit u
static inline i64 trunc_conv(i64 P,int u,int conv){
    switch(conv){ case 0: return P>=0? (P>>u) : -((~P)>>u);      // one's complement magnitude (current)
                  case 1: return P>=0? (P>>u) : -((-P)>>u);      // toward zero
                  case 2: return P>>u;                            // two's complement floor
                  case 3: return P>=0? (P>>u) : -(((-P)+(1ll<<u)-1)>>u);  // away from zero
                  default: return P>=0? ((P+(1ll<<(u-1)))>>u) : -(((-P)+(1ll<<(u-1)))>>u); } }  // nearest
// A3 latch conventions
static inline i64 a3_conv(i64 P,int conv){
    switch(conv){ case 0: return ((P+K)>>20)>>3;                  // current: floor twice
                  case 1: return (P+K)>>23;                       // single shift
                  case 2: return P>=0? (((P+K)>>20)>>3) : -((((~(P+K))>>20)>>3));
                  default: return P>=0? ((P+K)>>23) : -((~(P+K))>>23); } }
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts("node 0 + node 32 (2048 samples, no square term): conventions that reproduce them exactly");
    puts("c1conv c3conv a3conv | node0 miss  node32 miss");
    std::vector<std::array<int,3>> good;
    for(int c1=0;c1<5;c1++) for(int c3=0;c3<5;c3++) for(int a3=0;a3<4;a3++){
        int m0=0,m32=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=trunc_conv(arr(TB[0].C1f,dx)+K,23-TB[0].e1,c1);
            i64 A3=a3_conv(arr(((i64)t*t)<<3,dx),a3);
            i64 T3=trunc_conv(arr(TB[0].c3<<5,A3)+K,26,c3);
            for(int half=0;half<2;half++){ int node=half?32:0; i64 s=half?-1:1; i64 V=s*T1-s*T3;
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi){ if(half)m32++; else m0++; } } }
        if(m0==0&&m32==0){ printf("  %d      %d      %d   | exact       exact\n",c1,c3,a3); good.push_back(std::array<int,3>{c1,c3,a3}); } }
    printf("conventions exact on nodes 0 and 32: %zu\n\n",good.size());
    // for each, the symmetric-square floor over nodes 1..31
    puts("c1 c3 a3 | symmetric-J floor | free-J floor");
    for(auto&g:good){ int c1=g[0],c3=g[1],a3=g[2];
        auto viol=[&](int t,i64 J)->int{ int v=0; i64 dx=(i64)t<<13;
            i64 A3=a3_conv(arr(((i64)t*t)<<3,dx),a3);
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                i64 s=flip?-1:1;
                i64 T1=TB[n].C1f? trunc_conv(arr(TB[n].C1f,dx)+K,23-TB[n].e1,c1):0;
                i64 T3=TB[n].c3? trunc_conv(arr(TB[n].c3<<5,A3)+K,26,c3):0;
                i64 T2=sm(arr(J,TB[n].S)+K,U2TAB[n]);
                i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi)v++; } return v; };
        auto Jm=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
        int tsym=0,tfree=0;
        for(int a=0;a<=511;a++){ i64 m=Jm(a); int b=1<<30;
            for(i64 d=-120;d<=120;d++){ i64 J=m+d; if(J<0)continue; int v=viol(a,J)+(a?viol(-a,J):0); b=std::min(b,v); }
            tsym+=b; }
        for(int t=-512;t<=511;t++){ i64 m=Jm(t); int b=1<<30;
            for(i64 d=-120;d<=120;d++){ i64 J=m+d; if(J<0)continue; b=std::min(b,viol(t,J)); } tfree+=b; }
        printf(" %d  %d  %d | %5d             | %5d\n",c1,c3,a3,tsym,tfree); }
    return 0; }
