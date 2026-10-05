// c11_norm.cpp -- the one mechanism that breaks the lock.
// With a FIXED-alignment latch, one LSB is a constant, so the asymmetry and the small-offset resolution are
// both  t/2^(s2-13)  and their ratio is 17/342 = 1/20.1 -- the data needs 1/405.
// With a NORMALISED latch (24 significant bits + exponent) the LSB weight is proportional to the latch value,
// hence to |t|, so BOTH quantities scale as t^2 and the ratio becomes (17/342)^2 = 1/405.  Exactly the needed
// headroom.  Score the full model; filter on node 16 first (no linear or cubic term there, and it carries the
// three asymmetry witnesses t = -385, -411, -476).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static inline int nbits(i64 v){ if(v<0)v=-v; return v? 64-__builtin_clzll((u64)v):0; }
struct Cfg{ int W1,W2,U,r1,r2,r3,k1,k2,rn1,rn2; };
static inline i64 T2of(const Cfg&c,int t,int n,int&ok){
    i64 dx=(i64)t<<13; i64 K1=c.k1?(1ll<<19):0, K2=c.k2?(1ll<<19):0;
    i64 P1=(c.r1? arr(dx,KC):arr(KC,dx))+K1; int n1=nbits(P1)-c.W1; if(n1<0)n1=0;
    i64 m1 = c.rn1 ? ((P1 + (n1? (1ll<<(n1-1)):0))>>n1) : (P1>>n1);
    i64 P2=(c.r2? arr(dx,m1):arr(m1,dx))+K2; int n2=nbits(P2)-c.W2; if(n2<0)n2=0;
    i64 m2 = c.rn2 ? ((P2 + (n2? (1ll<<(n2-1)):0))>>n2) : (P2>>n2);
    int u=c.U+(TB[n].u2-30)-n1-n2; if(u<0||u>62){ ok=0; return 0; } ok=1;
    i64 P=c.r3? arr(TB[n].S,m2):arr(m2,TB[n].S); return sm(P+K,u); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    // node-16 constraints (V = C0 - T2 there)
    i64 lo16[1024],hi16[1024];
    for(int t=-512;t<=511;t++){ int a=(16*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        lo16[t+512]=C0TAB[16]-hi; hi16[t+512]=C0TAB[16]-lo; }
    std::vector<Cfg> cfgs;
    for(int W1=19;W1<=26;W1++) for(int W2=19;W2<=26;W2++) for(int U=40;U<=80;U++)
      for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
        for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int rn1=0;rn1<2;rn1++) for(int rn2=0;rn2<2;rn2++)
          cfgs.push_back({W1,W2,U,r1,r2,r3,k1,k2,rn1,rn2});
    printf("configs: %zu\n",cfgs.size());
    int best16=1<<30, bestTot=1<<30; Cfg b16{}, bTot{};
    #pragma omp parallel
    { int l16=1<<30,lt=1<<30; Cfg c16{},ct{};
      #pragma omp for schedule(dynamic)
      for(size_t i=0;i<cfgs.size();i++){ const Cfg&c=cfgs[i]; int m16=0,ok;
        for(int t=-512;t<=511;t++){ i64 T2=T2of(c,t,16,ok); if(!ok){ m16=1<<29; break; }
            if(T2<lo16[t+512]||T2>hi16[t+512]) m16++; if(m16>40) break; }
        if(m16<l16){ l16=m16; c16=c; }
        if(m16>6) continue;                                   // only promising ones get the full score
        int tot=0;
        for(int node=0;node<=31&&tot<lt;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 T2=TB[n].S? T2of(c,t,n,ok):0; if(!ok){ tot=1<<29; break; }
            i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            if(V<lo||V>hi) tot++; } }
        if(tot<lt){ lt=tot; ct=c; } }
      #pragma omp critical
      { if(l16<best16){best16=l16;b16=c16;} if(lt<bestTot){bestTot=lt;bTot=ct;} } }
    printf("best node-16 misses: %d  (W1=%d W2=%d U=%d r=%d%d%d k=%d%d rn=%d%d)\n",
        best16,b16.W1,b16.W2,b16.U,b16.r1,b16.r2,b16.r3,b16.k1,b16.k2,b16.rn1,b16.rn2);
    printf("best FULL score: %d window violations over nodes 0..31 (baseline 147)\n",bestTot);
    if(bestTot<(1<<28)) printf("  W1=%d W2=%d U=%d roles=%d%d%d K=%d%d round=%d%d\n",
        bTot.W1,bTot.W2,bTot.U,bTot.r1,bTot.r2,bTot.r3,bTot.k1,bTot.k2,bTot.rn1,bTot.rn2);
    return 0; }
