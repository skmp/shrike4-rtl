// wd_core.h -- parameterised structural FSCA model + exact metrics.
//   M1 = window violations over nodes 0..31 with the chain J   (baseline 148)
//   M2 = minimum violations with a FREE integer J(t) per t     (baseline 2)
//   M3 = number of t with an EMPTY J intersection              (baseline 2: t=+-17)
//   M4 = jwin score of the chain J (# feasible t satisfied)    (baseline 926)
// Interval inversion is exact: T2(J) is non-decreasing in J (b6.cpp), so a per-table
// threshold array gives the allowed J interval for every constraint without search.
#pragma once
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>

struct Cfg {
    int flipLin = 0;   // 0: -T1        1: sm(arr(C1f,-dx)+K,u)   2: sm(~arr(C1f,dx)+K,u)
    int flipCub = 0;   // 0: -T3        1: sm(arr(c3<<5,-A3)+K,26) 2: sm(~arr(c3<<5,A3)+K,26)
    int linK   = 0;    // 0: 2^19       1: 2^19+2^(u-1)   2: 2^(u-1)   3: 0    4: 2^19+2^(u-2)
    int cubK   = 0;    // 0: 2^19       1: 2^19+2^25      2: 2^25      3: 0
    int sqK    = 0;    // 0: 2^19       1: 0              2: 2^(u2-1)  3: 2^19+2^(u2-1)  4: use sqKv
    i64 sqKv   = 1ll<<19;  // explicit final-square rounding constant when sqK==4
    int a3K    = 0;    // A3 pre-shift rounding: 0: (arr+2^19)>>20>>3   1: (arr+2^19)>>23  2: (arr+2^22)>>23
    int Smode  = 0;    // 0: 23b RN (current) 1: 23b trunc 2: 24b RN 3: 23b half-up 4: 23b RN, node16 unclamped(2^23)
    int u2off  = 0;    // global u2 offset
    int Sw     = 0;    // if >0: sine field width w, S=RN(sin*2^(w+k)) clamped, u2 = w+7+k  (w=23 == baseline)
    int Sw_rnd = 0;    // 0 RN  1 trunc  2 half-up  3 ceil
    int i1K    = 0;    // 0: 2^19  1: 0  2: 2^22
    int i2K    = 0;    // 0: 2^19  1: 0  2: 2^20   3: use i2Kv / s2v
    i64 i2Kv   = 1ll<<19; int s2v = 20;  int u2base = 30;
    int r0=0,r1=0;  int s1v=23; i64 Av=0;   // roles of the two chain multiplies; first-stage shift/constant
    i64 Sov[17]  = {0};   // per-table S override (0 = derived)
    int u2ov[17] = {0};   // per-table u2 override (0 = derived)
    i64 C0ov[17] = {0};   // per-table C0 override (-1 = force 0, 0 = fit in window)
};

static Fsca FF;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){ Win w=decode(FF.srz[a],FF.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} ex=w.exact; }

struct Model {
    i64 S[17]; int u2[17]; i64 C0[17];
    Cfg c;
};

static i64 SvalW(int n,int w,int rnd,int&kout){
    i128 a=PI_()*n/32; i128 s=sin_(a); int k=0; if(n>0){ while(s*((i128)1<<(k+1))<QONE) k++; }
    kout=k; if(n==0) return 0; i64 lim=1ll<<w;
    i128 num = s << (w+k); i128 q=num/QONE, r=num%QONE;
    if(rnd==1){} else if(rnd==2){ if(r*2>=QONE) ++q; } else if(rnd==3){ if(r) ++q; }
    else { if(r*2>QONE||(r*2==QONE&&(q&1))) ++q; }
    if(q>=lim) q=lim-1; return (i64)q; }
static i64 Sval(int n, int mode, int& kout){
    i128 a=PI_()*n/32; i128 s=sin_(a);
    int k=0; if(n>0){ while(s*((i128)1<<(k+1))<QONE) k++; }
    kout=k; if(n==0) return 0;
    int w = (mode==2)?24:23;
    i64 lim = 1ll<<w;
    i128 num = s << (w+k); i128 q=num/QONE, r=num%QONE;
    if(mode==1){ /*trunc*/ }
    else if(mode==3){ if(r*2>=QONE) ++q; }
    else { if(r*2>QONE||(r*2==QONE&&(q&1))) ++q; }
    if(q>=lim){ if(mode==4) return lim; q=lim-1; }
    return (i64)q;
}

static inline i64 lin_term(const Tab&T,i64 dx,int flip,const Cfg&c){
    if(!T.C1f) return 0; int u=23-T.e1;
    i64 KK = c.linK==0?(1ll<<19) : c.linK==1?((1ll<<19)+(1ll<<(u-1))) : c.linK==2?(1ll<<(u-1)) : c.linK==3?0 : ((1ll<<19)+(1ll<<(u-2)));
    if(!flip) return sm(arr(T.C1f,dx)+KK,u);
    if(c.flipLin==1) return sm(arr(T.C1f,-dx)+KK,u);
    if(c.flipLin==2) return sm(~arr(T.C1f,dx)+KK,u);
    return -sm(arr(T.C1f,dx)+KK,u);
}
static inline i64 a3_of(int t,const Cfg&c){ i64 dx=(i64)t<<13; i64 A=arr(((i64)t*t)<<3,dx);
    if(c.a3K==1) return (A+(1ll<<19))>>23; if(c.a3K==2) return (A+(1ll<<22))>>23; return ((A+(1ll<<19))>>20)>>3; }
static inline i64 cub_term(const Tab&T,int t,int flip,const Cfg&c){
    if(!T.c3) return 0; i64 A3=a3_of(t,c);
    i64 KK = c.cubK==0?(1ll<<19) : c.cubK==1?((1ll<<19)+(1ll<<25)) : c.cubK==2?(1ll<<25) : 0;
    if(!flip) return sm(arr(T.c3<<5,A3)+KK,26);
    if(c.flipCub==1) return sm(arr(T.c3<<5,-A3)+KK,26);
    if(c.flipCub==2) return sm(~arr(T.c3<<5,A3)+KK,26);
    return -sm(arr(T.c3<<5,A3)+KK,26); }
static inline i64 sq_term(i64 J,i64 S,int u2,const Cfg&c){
    if(!S) return 0;
    i64 KK = c.sqK==4?c.sqKv : c.sqK==0?(1ll<<19) : c.sqK==1?0 : c.sqK==2?(1ll<<(u2-1)) : ((1ll<<19)+(1ll<<(u2-1)));
    return sm(arr(J,S)+KK,u2); }
static inline i64 chainJ(int t,const Cfg&c){ i64 dx=(i64)t<<13;
    i64 k1 = c.i1K==0?(1ll<<19) : c.i1K==1?0 : (1ll<<22);
    i64 k2 = c.i2K==3?c.i2Kv : c.i2K==0?(1ll<<19) : c.i2K==1?0 : (1ll<<20);
    i64 A = c.Av? c.Av : KC; int s1 = c.s1v;
    i64 I1=((c.r0?arr(dx,A):arr(A,dx))+k1)>>s1;
    return ((c.r1?arr(dx,I1):arr(I1,dx))+k2)>>(c.i2K==3?c.s2v:20); }

// base (everything but -T2) for table n, flip, offset t
static inline i64 base_of(const Model&M,int n,int t,int flip){
    const Tab&T=TB[n]; i64 dx=(i64)t<<13; i64 s=flip?-1:1;
    i64 L=lin_term(T,dx,flip,M.c); i64 Cu=cub_term(T,t,flip,M.c);
    (void)s; return M.C0[n] + L - Cu; }   // sign already inside the term functions

struct Con { i64 lo,hi; int tab; };      // T2 must be in [lo,hi]

struct Metrics { int M1,M2,M3,M4,nodeviol[32]; };

static void build_model(Model&M){
    build_tables();
    for(int n=0;n<=16;n++){ int k; i64 s = M.c.Sw? SvalW(n,M.c.Sw,M.c.Sw_rnd,k) : Sval(n,M.c.Smode,k);
        M.S[n]  = M.c.Sov[n]? M.c.Sov[n] : s;
        M.u2[n] = M.c.u2ov[n]? M.c.u2ov[n] : (M.c.Sw? (M.c.Sw+7+k+M.c.u2off) : (M.c.u2base+k+M.c.u2off));
    }
}

// fit C0 per table inside the t=0 window (both flips must satisfy it), choosing the value that
// minimises violations with the chain J; returns candidate list sizes too.
static void fit_C0(Model&M,int*ncand=nullptr){
    for(int n=0;n<=16;n++){
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        int bv=1<<30; i64 bc=lo0; int cnt=0;
        for(i64 cc=lo0-4;cc<=hi0+4;cc++){
            M.C0[n]=cc;
            // must satisfy t=0 for both flips
            bool ok=true; for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17);
                i64 V=base_of(M,n,0,flip)-sq_term(chainJ(0,M.c),M.S[n],M.u2[n],M.c);
                i64 l,h;bool e; win_of((m*1024)&0xffff,l,h,e); if(V<l||V>h) ok=false; }
            if(!ok) continue; cnt++;
            int v=0; for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17);
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
                    i64 V=base_of(M,n,t,flip)-sq_term(chainJ(t,M.c),M.S[n],M.u2[n],M.c);
                    if(V<l||V>h)v++; } }
            if(v<bv){bv=v;bc=cc;} }
        M.C0[n]= M.c.C0ov[n]? M.c.C0ov[n] : bc;
        if(ncand)ncand[n]=cnt; }
}

// threshold arrays: thr[tab][v] = min J with T2(J) >= v ; Vmax[tab]
static std::vector<i64> THR[17]; static i64 JMAX=12000000;
static void set_jmax(const Cfg&c){ int s2 = (c.i2K==3? c.s2v : 20); i64 b=10400000;
    JMAX = (20-s2>=0)? (b<<(20-s2)) : (b>>(s2-20)); if(JMAX>(1ll<<27)) JMAX=1ll<<27; }
static i64 CACHE_S[17]={0}; static int CACHE_U[17]={0}; static i64 CACHE_K[17]={-1};
static i64 LAST_JMAX=-1;
static void build_thr(const Model&M){
    set_jmax(M.c); if(LAST_JMAX!=JMAX){ LAST_JMAX=JMAX; for(int i=0;i<17;i++){CACHE_K[i]=-1; THR[i].clear();} }
#pragma omp parallel for schedule(dynamic)
    for(int n=0;n<=16;n++){
        i64 kk = M.c.sqK==4?M.c.sqKv : M.c.sqK==0?(1ll<<19) : M.c.sqK==1?0 : M.c.sqK==2?(1ll<<(M.u2[n]-1)) : ((1ll<<19)+(1ll<<(M.u2[n]-1)));
        if(CACHE_S[n]==M.S[n]&&CACHE_U[n]==M.u2[n]&&CACHE_K[n]==kk&&!THR[n].empty()) continue;
        if(CACHE_S[n]==M.S[n]&&CACHE_U[n]==M.u2[n]&&CACHE_K[n]==kk&&M.S[n]==0) continue;
        CACHE_S[n]=M.S[n]; CACHE_U[n]=M.u2[n]; CACHE_K[n]=kk;
        THR[n].clear(); if(!M.S[n]) continue;
        i64 vmax = sq_term(JMAX,M.S[n],M.u2[n],M.c); if(vmax<0||vmax>(1ll<<22)){ fprintf(stderr,"BAD vmax %lld n=%d\n",(long long)vmax,n); exit(2);} 
        THR[n].assign((size_t)vmax+2, JMAX+1);
        THR[n][0]=0;
        for(i64 v=1;v<=vmax;v++){ i64 a=0,b=JMAX;   // min J with T2(J) >= v  (T2 non-decreasing)
            while(a<b){ i64 m=a+(b-a)/2; if(sq_term(m,M.S[n],M.u2[n],M.c)>=v) b=m; else a=m+1; }
            THR[n][v]=a; }
    }
}
// allowed J interval for T2 in [lo,hi] on table n; returns false if empty
static inline bool jint(const Model&M,int n,i64 lo,i64 hi,i64&a,i64&b){
    const std::vector<i64>&t=THR[n]; i64 vmax=(i64)t.size()-2;
    if(hi<0) return false; if(lo>vmax) return false;
    a = lo<=0?0:t[lo]; b = (hi+1<=vmax)? t[hi+1]-1 : JMAX;
    if(a>JMAX) return false; return a<=b; }

static Metrics evaluate(const Model&M,bool verbose=false){
    Metrics R; memset(&R,0,sizeof(R)); build_thr(M);
    int M1=0,M2=0,M3=0,M4=0;
    // jwin recompute + per-t analysis
    for(int t=-512;t<=511;t++){
        i64 J=chainJ(t,M.c);
        std::vector<std::pair<i64,i64>> iv; int nc=0; int vio=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 b=base_of(M,n,t,flip);
            i64 V=b-sq_term(J,M.S[n],M.u2[n],M.c);
            if(V<l||V>h){ M1++; R.nodeviol[node]++; vio++; }
            if(!M.S[n]) continue;               // no J dependence
            nc++; i64 aa,bb; if(jint(M,n,b-h,b-l,aa,bb)) iv.push_back({aa,bb}); }
        // max coverage
        std::vector<std::pair<i64,int>> ev; for(auto&p:iv){ ev.push_back({p.first,+1}); ev.push_back({p.second+1,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bestJ=0;
        for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best){best=cur;bestJ=ev[i].first;} }
        M2 += nc-best; if(best<nc) M3++;
        if(best==nc){ // feasible: does the chain J satisfy all?
            bool ok=true; for(auto&p:iv) if(J<p.first||J>p.second) ok=false;
            if(ok) M4++; }
        (void)bestJ;
        if(verbose&&(nc-best)>0) printf("  t=%d  constraints=%d  max-consistent=%d  (chainJ=%lld)\n",t,nc,best,(long long)J);
    }
    R.M1=M1;R.M2=M2;R.M3=M3;R.M4=M4; return R; }
