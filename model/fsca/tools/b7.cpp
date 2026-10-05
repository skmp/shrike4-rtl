// b7.cpp -- search the square-term CHAIN (two multiplies producing the latch J) against the per-t
// allowed-J intervals derived from all 31 node instances.  The last multiply (J x S) is held fixed,
// so this decouples the chain from the coefficient tables completely.
//   J ~ KC*t^2*2^g / 2^17 ;  stage shifts satisfy s1+s2 = 43-g
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// rounding rules for a latch: 0 floor, 1 toward zero, 2 one's-complement magnitude (sm), 3 nearest(floor(x+1/2) already via K)
static inline i64 latch(i64 P,int s,int rule){
    switch(rule){ case 0: return P>>s; case 1: return P>=0? (P>>s) : -((-P)>>s); default: return P>=0? (P>>s) : -((~P)>>s); } }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int G = argc>1?atoi(argv[1]):0;                  // latch scale: J = I2 * 2^G, last shift u2+G
    // ---- allowed J interval per t
    std::vector<i64> JL(1024), JH(1024); std::vector<char> ok(1024,1);
    i64 JMAX = 12000000ll<<G;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=JMAX;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+G;
            i64 a1=0,b1=JMAX+1; while(a1<b1){ i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo) b1=m; else a1=m+1; }
            i64 a2=-1,b2=JMAX;   while(a2<b2){ i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi) a2=m; else b2=m-1; }
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; ok[t+512]= gl<=gh; }
    int nc=0; for(int i=0;i<1024;i++) nc+=ok[i];
    printf("G=%d: %d/1024 t with a non-empty allowed J interval (max score)\n",G,nc);
    // ---- enumerate chains
    struct Cfg{ int order,r1,r2,s1,k1,k2,rule1,rule2,sgn; i64 kc; };
    std::vector<Cfg> cfgs;
    for(int order=0;order<2;order++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
      for(int s1=8;s1<=35;s1++) for(int k1=0;k1<3;k1++) for(int k2=0;k2<3;k2++)
        for(int rule1=0;rule1<3;rule1++) for(int rule2=0;rule2<3;rule2++) for(int sgn=0;sgn<2;sgn++)
          for(int dk=-2;dk<=2;dk++){ int s2=43-G-s1; if(s2<1||s2>40) continue;
            cfgs.push_back({order,r1,r2,s1,k1,k2,rule1,rule2,sgn,0x4ef145+dk}); }
    std::vector<int> score(cfgs.size(),0);
    #pragma omp parallel for schedule(static)
    for(size_t ci=0;ci<cfgs.size();ci++){ Cfg c=cfgs[ci]; int s2=43-G-c.s1;
        i64 K1 = c.k1==0?0 : c.k1==1?(1ll<<19) : (c.s1>0?(1ll<<(c.s1-1)):0);
        i64 K2 = c.k2==0?0 : c.k2==1?(1ll<<19) : (s2>0?(1ll<<(s2-1)):0);
        i64 kc = c.sgn? -c.kc : c.kc; int sc=0;
        for(int t=-512;t<=511;t++){ if(!ok[t+512]) continue; i64 dx=(i64)t<<13; i64 J;
            if(c.order==0){ i64 P1 = c.r1? arr(dx,kc) : arr(kc,dx); i64 I1=latch(P1+K1,c.s1,c.rule1);
                i64 P2 = c.r2? arr(dx,I1) : arr(I1,dx); J=latch(P2+K2,s2,c.rule2); }
            else { i64 P1 = arr(dx,dx); i64 I1=latch(P1+K1,c.s1,c.rule1);
                i64 P2 = c.r2? arr(kc,I1) : arr(I1,kc); J=latch(P2+K2,s2,c.rule2); }
            if(J>=JL[t+512]&&J<=JH[t+512]) sc++; }
        score[ci]=sc; }
    std::vector<size_t> idx(cfgs.size()); for(size_t i=0;i<idx.size();i++)idx[i]=i;
    std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return score[a]>score[b];});
    printf("best configs (order: 0=KC*dx then *dx, 1=dx*dx then *KC):\n");
    printf("score | order r1 r2 s1 s2 k1 k2 rule1 rule2 sgn kc\n");
    for(int i=0;i<25;i++){ size_t ci=idx[i]; Cfg c=cfgs[ci];
        printf("%5d |   %d    %d  %d %2d %2d  %d  %d    %d     %d   %d  %06llx\n",score[ci],c.order,c.r1,c.r2,c.s1,43-G-c.s1,c.k1,c.k2,c.rule1,c.rule2,c.sgn,(long long)c.kc); }
    return 0; }
