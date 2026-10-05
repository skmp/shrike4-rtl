// b30.cpp -- focused exploration of the two-factor square family, reporting per-node scores so near-misses are
// visible.  The asymmetry ladder argument says the sign asymmetry in latch units is t * 2^(7+q-u3) where the
// node-dependent factor is B_n ~ sin * t * 2^q; to reach the 6 that |t|=342 demands we need 7+q-u3 = -6.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 CLO[32][1024],CHI[32][1024]; static int TABN[32];
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; TABN[node]=n; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CLO[node][t+512]=base-hi; CHI[node][t+512]=base-lo; } }
    // sine field variants for the second factor
    // 0: S_n (23-bit, shift sB+k)   1: S_n unnormalised (shift sB)   2: C1f[16-n] (shift sB - e1[16-n])
    struct R{ int sA,sB,u3,sv,r1,r2,r3,k1,k2,k3; i64 kq; int tot; int per[32]; };
    std::vector<R> cfg;
    for(int sA=20;sA<=28;sA++) for(int sB=14;sB<=30;sB++) for(int d=-2;d<=2;d++){ int u3=73+d-sA-sB; if(u3<6||u3>36)continue;
        for(int sv=0;sv<3;sv++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
          for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++){
            i64 kq = KC * (d>=0? (1ll<<d) : 1) / (d<0? (1ll<<(-d)) : 1);
            cfg.push_back({sA,sB,u3,sv,r1,r2,r3,k1,k2,k3,kq,0,{0}}); } }
    printf("configs: %zu\n",cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfg.size();ci++){ R&c=cfg[ci];
        i64 K1=c.k1?(1ll<<19):0, K2=c.k2?(1ll<<19):0, K3=c.k3?(1ll<<19):0; int tot=0;
        for(int node=1;node<=31;node++){ int n=TABN[node]; int kk=TB[n].u2-30; int v=0;
            i64 Fs; int shB;
            if(c.sv==0){ Fs=TB[n].S; shB=c.sB+kk; }
            else if(c.sv==1){ Fs=TB[n].S; shB=c.sB; }
            else { int m=16-n; Fs=TB[m].C1f; shB=c.sB-TB[m].e1; }
            if(shB<0||shB>60){ v=1024; tot+=v; c.per[node]=v; continue; }
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T2;
                if(!Fs) T2=0; else { i64 B=((c.r1? arr(dx,Fs):arr(Fs,dx))+K2)>>shB;
                    i64 A=((c.r2? arr(dx,c.kq):arr(c.kq,dx))+K1)>>c.sA;
                    i64 P=c.r3? arr(B,A):arr(A,B); T2=sm(P+K3,c.u3); }
                if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) v++; }
            c.per[node]=v; tot+=v; }
        c.tot=tot; }
    std::sort(cfg.begin(),cfg.end(),[](const R&a,const R&b){return a.tot<b.tot;});
    printf("tot | sA sB u3 sv r1 r2 r3 k1k2k3 Kq        | guard(1,2,16,31) | per-node 3..15\n");
    for(int i=0;i<20;i++){ R&r=cfg[i];
        printf("%4d | %2d %2d %2d  %d  %d  %d  %d  %d%d%d %9lld | %d %d %d %d |",r.tot,r.sA,r.sB,r.u3,r.sv,r.r1,r.r2,r.r3,
            r.k1,r.k2,r.k3,(long long)r.kq,r.per[1],r.per[2],r.per[16],r.per[31]);
        for(int n=3;n<=15;n++) printf(" %d",r.per[n]); printf("\n"); }
    return 0; }
