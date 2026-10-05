// b24.cpp -- is the square latch really asymmetric, or is that an artifact of my derived table values?
// Alternate: (a) optimise a SYMMETRIC latch J(|t|) given the sine fields, (b) optimise each stored sine field
// S_n (within +-3 of RN23(sin)) and C0_n (within its t=0 window) given J.  If the symmetric floor drops from
// 70 to near 0, the asymmetry was an artifact.  Also reports the same for a free asymmetric J as a control.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 SV[17],C0V[17],c0lo[17],c0hi[17],S0[17];
static i64 BASE0[32][1024];        // C0-independent part: s*T1 - s*T3
static i64 WLO[32][1024],WHI[32][1024]; static char USE[32]; static int TAB[32],FLIP[32];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int n=0;n<=16;n++){ i64 l,h;bool e; win_of(n*1024,l,h,e); c0lo[n]=l;c0hi[n]=h;
        S0[n]=TB[n].S; SV[n]=TB[n].S; C0V[n]=std::max(c0lo[n],std::min(c0hi[n],TB[n].sin26)); }
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; USE[node]=TB[n].S!=0; TAB[node]=n; FLIP[node]=flip;
        if(!USE[node])continue; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            BASE0[node][t+512]=s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            WLO[node][t+512]=lo; WHI[node][t+512]=hi; } }
    auto viol_t=[&](int t,i64 J)->int{ int v=0; for(int node=1;node<=31;node++){ if(!USE[node])continue; int n=TAB[node];
            i64 T2=sm(arr(J,SV[n])+K,U2TAB[n]); i64 V=C0V[n]+BASE0[node][t+512]-T2;
            if(V<WLO[node][t+512]||V>WHI[node][t+512])v++; } return v; };
    auto Jm=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
    std::vector<i64> Jsym(512),Jfree(1024);
    auto optJ=[&](bool sym)->int{ int tot=0;
        if(sym){ for(int a=0;a<=511;a++){ i64 m=Jm(a); int b=1<<30; i64 bj=m;
                for(i64 d=-80;d<=80;d++){ i64 J=m+d; if(J<0)continue; int v=viol_t(a,J)+(a?viol_t(-a,J):0); if(v<b){b=v;bj=J;} }
                Jsym[a]=bj; tot+=b; } }
        else { for(int t=-512;t<=511;t++){ i64 m=Jm(t); int b=1<<30; i64 bj=m;
                for(i64 d=-80;d<=80;d++){ i64 J=m+d; if(J<0)continue; int v=viol_t(t,J); if(v<b){b=v;bj=J;} }
                Jfree[t+512]=bj; tot+=b; } }
        return tot; };
    int cur=optJ(true); printf("start: symmetric-J violations = %d\n",cur);
    for(int iter=0;iter<8;iter++){ bool improved=false;
        for(int n=1;n<=16;n++){ i64 bS=SV[n],bC=C0V[n]; int bv=1<<30;
            for(i64 ds=-3;ds<=3;ds++) for(i64 c=c0lo[n];c<=c0hi[n];c++){ SV[n]=S0[n]+ds; C0V[n]=c;
                int v=0; for(int a=0;a<=511;a++){ i64 J=Jsym[a]; v+=viol_t(a,J)+(a?viol_t(-a,J):0); }
                if(v<bv){bv=v;bS=SV[n];bC=c;} }
            SV[n]=bS; C0V[n]=bC; if(bv<cur){cur=bv;improved=true;} }
        int nj=optJ(true); if(nj<cur){cur=nj;improved=true;} else cur=nj;
        printf("iter %d: symmetric-J violations = %d\n",iter,cur); if(!improved) break; }
    printf("\nfinal symmetric-J violations: %d\n",cur);
    printf("sine fields (delta from RN23(sin)) :"); for(int n=1;n<=16;n++) printf(" %+lld",(long long)(SV[n]-S0[n])); printf("\n");
    printf("C0 (delta from window low)         :"); for(int n=1;n<=16;n++) printf(" %+lld",(long long)(C0V[n]-c0lo[n])); printf("\n");
    printf("free-J violations with these tables: %d\n",optJ(false));
    return 0; }
