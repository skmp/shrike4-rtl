// b21.cpp -- where can the required sign asymmetry live?
//  (1) free symmetric J(|t|), model cubic               (known: 70)
//  (2) free symmetric J(|t|) + free per-t cubic latch A3
//  (3) free symmetric J(|t|) + free per-t offset on dx used by the LINEAR term only
//  (4) free per-t offset on dx shared by all three terms, model chain otherwise
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
struct NI{ int node,n,flip; };
static std::vector<NI> NIS;
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; NIS.push_back({node,n,flip}); }
    auto viol=[&](int t,i64 J,int dA3,int ddx)->int{ i64 dx=((i64)t<<13)+ddx; int v=0;
        i64 A3=(((arr(((i64)t*t)<<3,dx)+K)>>20)>>3)+dA3;
        for(auto&q:NIS){ const Tab&T=TB[q.n]; i64 s=q.flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 T2=sm(arr(J,T.S)+K,U2TAB[q.n]);
            i64 V=C0TAB[q.n]+s*T1-T2-s*T3; int a=(q.node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            if(V<lo||V>hi)v++; } return v; };
    auto Jm=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
    // candidate J values near the model
    auto cands=[&](int t){ std::vector<i64> c; i64 m=Jm(t); for(i64 d=-60;d<=60;d++) if(m+d>=0) c.push_back(m+d); return c; };
    int t1=0,t2=0,t3=0,t4=0;
    for(int a=0;a<=511;a++){
        std::vector<i64> cd=cands(a); if(a){ for(i64 v:cands(-a)) cd.push_back(v); }
        std::sort(cd.begin(),cd.end()); cd.erase(std::unique(cd.begin(),cd.end()),cd.end());
        int b1=1<<30,b2=1<<30,b3=1<<30;
        for(i64 J:cd){ int v=viol(a,J,0,0)+(a?viol(-a,J,0,0):0); b1=std::min(b1,v);
            int w=0; { int bw=1<<30; for(int d=-12;d<=12;d++) bw=std::min(bw,viol(a,J,d,0)); w+=bw; }
            if(a){ int bw=1<<30; for(int d=-12;d<=12;d++) bw=std::min(bw,viol(-a,J,d,0)); w+=bw; }
            b2=std::min(b2,w);
            int x=0; { int bx=1<<30; for(int d=-4;d<=4;d++) bx=std::min(bx,viol(a,J,0,d)); x+=bx; }
            if(a){ int bx=1<<30; for(int d=-4;d<=4;d++) bx=std::min(bx,viol(-a,J,0,d)); x+=bx; }
            b3=std::min(b3,x); }
        t1+=b1; t2+=b2; t3+=b3; }
    for(int t=-512;t<=511;t++){ int b=1<<30; for(int d=-8;d<=8;d++) b=std::min(b,viol(t,Jm(t),0,d)); t4+=b; }
    printf("(1) symmetric J, model cubic                       : %d\n",t1);
    printf("(2) symmetric J + free per-t cubic latch A3        : %d\n",t2);
    printf("(3) symmetric J + free per-t dx offset (all terms) : %d\n",t3);
    printf("(4) model J + free per-t dx offset                 : %d\n",t4);
    return 0; }
