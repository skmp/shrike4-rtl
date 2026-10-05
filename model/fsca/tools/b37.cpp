// b37.cpp -- FLOATING-POINT LATCHES.  The finer-latch result (free latch at shift 17/18 -> 0 violations) needs
// 26-27 bits, which no 24-bit multiplier operand can hold at a fixed alignment.  A normalised latch can: it
// keeps 24 SIGNIFICANT bits and carries an exponent, so its absolute resolution follows the magnitude -- fine
// at small |t|, coarse at large |t|.  That is exactly what a floating-point multiplier's output latch does.
//   X1 = arr(KC,dx) + K1 ; n1 = bits(X1)-24 ; m1 = X1 >> n1        (value = m1 * 2^n1)
//   X2 = arr(m1,dx) + K2 ; n2 = bits(X2)-24 ; m2 = X2 >> n2        (value = m2 * 2^(n1+n2))
//   T2 = sm(arr(m2,S_n) + K3, U + k_n - n1 - n2)
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 CLO[32][1024],CHI[32][1024]; static int TABN[32];
static inline int nbits(i64 v){ if(v<0)v=-v; return v? 64-__builtin_clzll((u64)v) : 0; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; TABN[node]=n; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CLO[node][t+512]=base-hi; CHI[node][t+512]=base-lo; } }
    struct R{ int W1,W2,r1,r2,r3,k1,k2,k3,U,rn1,rn2; int guard,viol; };
    std::vector<R> cfg;
    for(int W1=22;W1<=25;W1++) for(int W2=22;W2<=25;W2++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
      for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++)
        for(int rn1=0;rn1<2;rn1++) for(int rn2=0;rn2<2;rn2++) for(int U=30;U<=80;U++)
            cfg.push_back({W1,W2,r1,r2,r3,k1,k2,k3,U,rn1,rn2,0,0});
    printf("configs: %zu\n",cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfg.size();ci++){ R&c=cfg[ci];
        i64 K1=c.k1?(1ll<<19):0, K2=c.k2?(1ll<<19):0, K3=c.k3?(1ll<<19):0;
        int guard=0,viol=0; const int gn[4]={1,2,16,31};
        auto T2of=[&](int t,int n)->i64{ if(!TB[n].S) return 0; i64 dx=(i64)t<<13;
            i64 X1=(c.r1? arr(dx,KC):arr(KC,dx))+K1; int n1=nbits(X1)-c.W1; if(n1<0)n1=0;
            i64 m1 = c.rn1? ((X1+(n1?(1ll<<(n1-1)):0))>>n1) : (X1>>n1);
            i64 X2=(c.r2? arr(dx,m1):arr(m1,dx))+K2; int n2=nbits(X2)-c.W2; if(n2<0)n2=0;
            i64 m2 = c.rn2? ((X2+(n2?(1ll<<(n2-1)):0))>>n2) : (X2>>n2);
            int u=c.U+(TB[n].u2-30)-n1-n2; if(u<0||u>62) return -(1ll<<40);
            i64 P=c.r3? arr(TB[n].S,m2):arr(m2,TB[n].S); return sm(P+K3,u); };
        for(int gi=0;gi<4&&!guard;gi++){ int node=gn[gi]; int n=TABN[node];
            for(int t=-512;t<=511;t++){ i64 T2=T2of(t,n); if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) guard++; } }
        c.guard=guard; if(guard>3){ c.viol=-1; continue; }
        for(int node=1;node<=31;node++){ int n=TABN[node];
            for(int t=-512;t<=511;t++){ i64 T2=T2of(t,n); if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) viol++; } }
        c.viol=viol; }
    std::sort(cfg.begin(),cfg.end(),[](const R&a,const R&b){ int av=a.viol<0?1<<28:a.viol, bv=b.viol<0?1<<28:b.viol;
        if(av!=bv) return av<bv; return a.guard<b.guard; });
    printf("guard viol | W1 W2 r1 r2 r3 k1k2k3 rn1 rn2 U\n");
    int shown=0; for(auto&r:cfg){ if(r.viol<0) break;
        printf("%5d %4d | %2d %2d  %d  %d  %d  %d%d%d   %d   %d  %d\n",r.guard,r.viol,r.W1,r.W2,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,r.rn1,r.rn2,r.U);
        if(++shown>=15)break; }
    if(!shown){ printf("none guard-clean; best guard scores:"); for(int i=0;i<6;i++) printf(" %d",cfg[i].guard); printf("\n");
        R&r=cfg[0]; printf("best: W1=%d W2=%d r=%d%d%d k=%d%d%d rn=%d%d U=%d\n",r.W1,r.W2,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,r.rn1,r.rn2,r.U); }
    return 0; }
