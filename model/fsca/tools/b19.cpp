// b19.cpp -- optimised version of b18: precompute the per-(node,t) T2 constraint intervals once, precompute
// the two latch tables, then screen every config on the guard nodes (1,2,16 and mirrors: currently exact)
// before scoring survivors over all 31 node instances.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 C1S[17]; static int ES[17];
static void build_sine_linear(){ for(int n=0;n<=16;n++){ if(n==0){C1S[0]=0;ES[0]=0;continue;}
        i128 s=sin_(PI_()*n/32); i128 v=s*PI_()/QONE*((i128)1<<22); int e=0;
        while(v<((i128)1<<23)*QONE){ v<<=1; --e; }
        i128 q=v/(2*QONE), r=v%(2*QONE); if(r>QONE||(r==QONE&&(q&1)))++q; C1S[n]=(i64)q; ES[n]=e; } }
// constraint: T2 must be in [lo,hi] for this (node instance, t)
static i64 CLO[32][1024], CHI[32][1024]; static char CUSE[32];
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); build_sine_linear();
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; CUSE[node]= C1S[n]!=0;
        if(!CUSE[node]) continue; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CLO[node][t+512]=base-hi; CHI[node][t+512]=base-lo; } }
    const int S1LO=18,S1HI=32,S2LO=18,S2HI=32;
    // Ls[n][k1][r1][s1][t] and Q[k2][r2][dk][s2][t]
    static std::vector<i64> LS; LS.resize(17*3*2*(S1HI-S1LO+1)*1024);
    auto LSat=[&](int n,int k1,int r1,int s1,int t)->i64&{ return LS[(((( (size_t)n*3+k1)*2+r1)*(S1HI-S1LO+1)+(s1-S1LO))*1024)+t+512]; };
    for(int n=0;n<=16;n++) for(int k1=0;k1<3;k1++) for(int r1=0;r1<2;r1++) for(int s1=S1LO;s1<=S1HI;s1++){
        int sh=s1-ES[n]; i64 K1=k1==0?0:k1==1?(1ll<<19):(1ll<<(sh-1));
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 p=C1S[n]?(r1? arr(dx,C1S[n]):arr(C1S[n],dx)):0;
            LSat(n,k1,r1,s1,t)= C1S[n]? ((p+K1)>>sh) : 0; } }
    static std::vector<i64> QQ; QQ.resize(3*2*5*(S2HI-S2LO+1)*1024);
    auto Qat=[&](int k2,int r2,int dk,int s2,int t)->i64&{ return QQ[(((( (size_t)k2*2+r2)*5+dk)*(S2HI-S2LO+1)+(s2-S2LO))*1024)+t+512]; };
    for(int k2=0;k2<3;k2++) for(int r2=0;r2<2;r2++) for(int dk=0;dk<5;dk++) for(int s2=S2LO;s2<=S2HI;s2++){
        i64 kpi=6588397+dk-2; i64 K2=k2==0?0:k2==1?(1ll<<19):(1ll<<(s2-1));
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 p=r2? arr(dx,kpi):arr(kpi,dx); Qat(k2,r2,dk,s2,t)=(p+K2)>>s2; } }
    struct R{ int s1,s2,u3,r1,r2,r3,k1,k2,k3,dk; int guard,viol; };
    std::vector<R> cfg;
    for(int s1=S1LO;s1<=S1HI;s1++) for(int s2=S2LO;s2<=S2HI;s2++){ for(int tot=71;tot<=75;tot++){ int u3=tot-s1-s2; if(u3<8||u3>38)continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
          for(int k1=0;k1<3;k1++) for(int k2=0;k2<3;k2++) for(int k3=0;k3<3;k3++) for(int dk=0;dk<5;dk++)
            cfg.push_back({s1,s2,u3,r1,r2,r3,k1,k2,k3,dk,-1,-1}); } }
    printf("configs: %zu\n",cfg.size());
    const int guardn[6]={1,2,16,31,30,16};
    #pragma omp parallel for schedule(dynamic)
    for(size_t ci=0;ci<cfg.size();ci++){ R&c=cfg[ci];
        i64 K3=c.k3==0?0:c.k3==1?(1ll<<19):(1ll<<(c.u3-1));
        int g=0;
        for(int gi=0;gi<5;gi++){ int node=guardn[gi]; int flip=node>=17,n=flip?32-node:node; if(!CUSE[node])continue;
            for(int t=-512;t<=511;t++){ i64 Ls=LSat(n,c.k1,c.r1,c.s1,t), Q=Qat(c.k2,c.r2,c.dk,c.s2,t);
                i64 P=c.r3? arr(Q,Ls):arr(Ls,Q); i64 T2=sm(P+K3,c.u3);
                if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) g++; } }
        c.guard=g; if(g) { c.viol=-1; continue; }
        int v=0; for(int node=1;node<=31;node++){ if(!CUSE[node])continue; int flip=node>=17,n=flip?32-node:node;
            for(int t=-512;t<=511;t++){ i64 Ls=LSat(n,c.k1,c.r1,c.s1,t), Q=Qat(c.k2,c.r2,c.dk,c.s2,t);
                i64 P=c.r3? arr(Q,Ls):arr(Ls,Q); i64 T2=sm(P+K3,c.u3);
                if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) v++; } }
        c.viol=v; }
    std::vector<R*> ok; for(auto&c:cfg) if(c.guard==0) ok.push_back(&c);
    printf("configs exact on the guard nodes (1,2,16,30,31): %zu\n",ok.size());
    std::sort(ok.begin(),ok.end(),[](R*a,R*b){return a->viol<b->viol;});
    printf("viol | s1 s2 u3 r1 r2 r3 k1 k2 k3 kpi\n");
    for(size_t i=0;i<ok.size()&&i<25;i++){ R*r=ok[i]; printf("%5d | %2d %2d %2d  %d  %d  %d  %d  %d  %d  %lld\n",
        r->viol,r->s1,r->s2,r->u3,r->r1,r->r2,r->r3,r->k1,r->k2,r->k3,(long long)(6588397+r->dk-2)); }
    if(ok.empty()){ std::sort(cfg.begin(),cfg.end(),[](const R&a,const R&b){return a.guard<b.guard;});
        printf("best guard scores: "); for(int i=0;i<10;i++) printf("%d ",cfg[i].guard); printf("\n");
        R&r=cfg[0]; printf("best guard config: s1=%d s2=%d u3=%d r=%d%d%d k=%d%d%d kpi=%lld\n",r.s1,r.s2,r.u3,r.r1,r.r2,r.r3,r.k1,r.k2,r.k3,(long long)(6588397+r.dk-2)); }
    return 0; }
