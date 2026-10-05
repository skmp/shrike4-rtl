// b29.cpp -- TWO-FACTOR SQUARE with a NODE-DEPENDENT second factor:
//    A   = latch(arr(Kq,   dx) + K1, sA)            ~ 2pi^2_econ * t  (node independent)
//    B_n = latch(arr(S_n,  dx) + K2, sB + k_n)      ~ sin_n * t       (k_n = the sine field's octave)
//    T2  = sm(arr(A, B_n) + K3, u3)                 ~ 2pi^2 dx^2 sin
// Scale: sA + sB + u3 = 73 with Kq = KC.  The sign asymmetry is then delta_A * |B_n| / 2^u3, i.e. proportional
// to sin, which is why a single shared latch value fitted all nodes -- but the truncation of B_n is node
// dependent, so this is NOT equivalent to any shared-latch chain.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 CLO[32][1024],CHI[32][1024]; static int TABN[32];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; TABN[node]=n; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CLO[node][t+512]=base-hi; CHI[node][t+512]=base-lo; } }
    struct R{ int sA,sB,u3,r1,r2,r3,k1,k2,k3; i64 kq; int guard,viol; };
    std::vector<R> cfg;
    for(int sA=18;sA<=30;sA++) for(int sB=16;sB<=28;sB++){ int u3=73-sA-sB; if(u3<8||u3>34)continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
          for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++)
            for(i64 dq=-6;dq<=6;dq++) cfg.push_back({sA,sB,u3,r1,r2,r3,k1,k2,k3,KC+dq,0,0}); }
    printf("configs: %zu\n",cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfg.size();ci++){ R&c=cfg[ci];
        i64 K1=c.k1?(1ll<<19):0, K2=c.k2?(1ll<<19):0, K3=c.k3?(1ll<<19):0;
        int guard=0,viol=0;
        const int gn[4]={1,2,16,31};
        for(int gi=0;gi<4&&!guard;gi++){ int node=gn[gi]; int n=TABN[node]; int kk=TB[n].u2-30;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 B=((c.r1? arr(dx,TB[n].S):arr(TB[n].S,dx))+K2)>>(c.sB+kk);
                i64 A=((c.r2? arr(dx,c.kq):arr(c.kq,dx))+K1)>>c.sA;
                i64 P=c.r3? arr(B,A):arr(A,B); i64 T2=sm(P+K3,c.u3);
                if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) guard++; } }
        c.guard=guard; if(guard){ c.viol=-1; continue; }
        for(int node=1;node<=31;node++){ int n=TABN[node]; int kk=TB[n].u2-30;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 B=((c.r1? arr(dx,TB[n].S):arr(TB[n].S,dx))+K2)>>(c.sB+kk);
                i64 A=((c.r2? arr(dx,c.kq):arr(c.kq,dx))+K1)>>c.sA;
                i64 P=c.r3? arr(B,A):arr(A,B); i64 T2=sm(P+K3,c.u3);
                if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) viol++; } }
        c.viol=viol; }
    std::sort(cfg.begin(),cfg.end(),[](const R&a,const R&b){ if(a.guard!=b.guard)return a.guard<b.guard;
        if(a.guard) return false; return a.viol<b.viol; });
    printf("guard viol | sA sB u3 r1 r2 r3 k1 k2 k3 Kq\n");
    int shown=0; for(auto&r:cfg){ if(r.guard)break; printf("%5d %4d | %2d %2d %2d  %d  %d  %d  %d  %d  %d  %lld\n",
        r.guard,r.viol,r.sA,r.sB,r.u3,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,(long long)r.kq); if(++shown>=20)break; }
    if(!shown){ printf("no config exact on the guard nodes; best guard scores: ");
        for(int i=0;i<8;i++) printf("%d ",cfg[i].guard); printf("\n");
        R&r=cfg[0]; printf("best: sA=%d sB=%d u3=%d r=%d%d%d k=%d%d%d Kq=%lld\n",r.sA,r.sB,r.u3,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,(long long)r.kq); }
    return 0; }
