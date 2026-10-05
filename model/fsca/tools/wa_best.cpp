// wa_best.cpp -- family best score SUBJECT TO keeping the guard nodes (1,2,16,30,31) exact,
// i.e. K1,K2 must satisfy every guard window as well.  Also reports the unconstrained best.
#include "wa_common.h"
#include <omp.h>
static Fsca F; static Windows W,G;
static const i64 C0B[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2B[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static void build_guard(){ int gn[5]={1,2,16,30,31}; G.nfeas=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int gi=0;gi<5;gi++){ int node=gn[gi]; int flip=node>=17,n=flip?32-node:node;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0B[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2B[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        G.JL[t+512]=gl;G.JH[t+512]=gh;G.ok[t+512]=gl<=gh;G.nfeas+=G.ok[t+512]; } }
// guard-constrained solve
static int solveg(const Cfg&C,const std::vector<int>&TS,const std::vector<int>&TG,i64 KR,
                  i64*bK1=nullptr,i64*bK2=nullptr){
    std::vector<TPre> ps,pg; if(!prep(C,W,TS,ps,KR)) return -1; if(!prep(C,G,TG,pg,KR)) return -1;
    size_t n=TS.size(), m=TG.size(); i64 S1=1ll<<C.s1;
    std::vector<std::pair<i64,int>> bps;                       // (breakpoint, index) index<0 => guard
    for(size_t i=0;i<n;i++) bps.push_back({ps[i].bp,(int)i});
    for(size_t i=0;i<m;i++) bps.push_back({pg[i].bp,-(int)i-1});
    std::sort(bps.begin(),bps.end());
    std::vector<char> ss(n,0), sg(m,0);
    std::vector<std::pair<i64,int>> ev; int best=-1; i64 k1=0,k2=0;
    for(size_t stage=0;stage<=bps.size();stage++){
        i64 cur1;
        if(stage==0) cur1=0;
        else { if(bps[stage-1].first>=S1) break; int id=bps[stage-1].second;
               if(id>=0) ss[id]=1; else sg[-id-1]=1; cur1=bps[stage-1].first;
               if(stage<bps.size()&&bps[stage].first==cur1) continue; }
        i64 gl=-KR-1, gh=KR+1; bool bad=false;
        for(size_t i=0;i<m;i++){ int c=sg[i]; if(!pg[i].okiv[c]){bad=true;break;}
            if(pg[i].lo[c]>gl)gl=pg[i].lo[c]; if(pg[i].hi[c]<gh)gh=pg[i].hi[c]; }
        if(bad||gl>gh) continue;
        ev.clear();
        for(size_t i=0;i<n;i++){ int c=ss[i]; if(!ps[i].okiv[c])continue;
            i64 a=std::max(ps[i].lo[c],gl), b=std::min(ps[i].hi[c],gh); if(a>b)continue;
            ev.push_back({a,1}); ev.push_back({b+1,-1}); }
        if(ev.empty()){ if(best<0){best=0;k1=cur1;k2=gl;} continue; }
        std::sort(ev.begin(),ev.end()); int cu=0; size_t i=0;
        while(i<ev.size()){ i64 x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cu+=ev[i].second;i++;}
            if(cu>best){best=cu;k1=cur1;k2=x;} }
    }
    if(bK1)*bK1=k1; if(bK2)*bK2=k2; return best; }
struct Ent{ Cfg c; int sc0,scF; i64 k1,k2; };
int main(){ if(!load_fsca(F))return 1; build_tables(); if(!load_windows(W))return 1; build_guard();
    std::vector<int> ALLT,GT; for(int t=-512;t<=511;t++){ if(W.ok[t+512])ALLT.push_back(t); if(G.ok[t+512])GT.push_back(t); }
    std::vector<int> T0; for(int t=-496;t<=496;t+=31) T0.push_back(t);
    std::vector<std::pair<i64,int>> csts;
    for(int j=0;j<=2;j++) for(int d=-8;d<=8;d++){ csts.push_back({((i64)(0x4ef145+d))<<j,j}); csts.push_back({-(((i64)(0x4ef145+d))<<j),j}); }
    struct Outer{ i64 cst; int g1,s1,r1,rule1,m1; }; std::vector<Outer> outs;
    for(auto&cc:csts) for(int g1=8;g1<=15;g1++) for(int e=-9;e<=-3;e++) for(int r1=0;r1<2;r1++)
        for(int rule1=0;rule1<7;rule1++) for(int m1=-1;m1<=1;m1++){ int s1=g1+17+e+cc.second;
            if(s1<4||s1>44)continue; outs.push_back({cc.first,g1,s1,r1,rule1,m1}); }
    int MM[9]={0,1,2,3,4,9,10,11,12}; int nth=omp_get_max_threads();
    std::vector<std::vector<Ent>> keep(nth); std::vector<int> thr(nth,0); const size_t CAP=20000;
    double t0=omp_get_wtime();
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t oi=0;oi<outs.size();oi++){ Outer O=outs[oi]; int tid=omp_get_thread_num(); int e=O.s1-O.g1-17;
        for(int mi=0;mi<9;mi++) for(int g2=8;g2<=15;g2++){ int s2=g2-e; if(s2<4||s2>44)continue;
          for(int r2=0;r2<2;r2++){ int nr2=O.cst<0?7:1;
            for(int rule2=0;rule2<nr2;rule2++){ Cfg c{O.cst,O.g1,O.s1,O.r1,O.rule1,MM[mi],O.m1,g2,s2,r2,rule2};
                int s=solveg(c,T0,T0,1ll<<(s2+1));
                if(s>=thr[tid]){ keep[tid].push_back({c,s,0,0,0});
                    if(keep[tid].size()>CAP*2){ std::nth_element(keep[tid].begin(),keep[tid].begin()+CAP,keep[tid].end(),
                        [](const Ent&a,const Ent&b){return a.sc0>b.sc0;}); keep[tid].resize(CAP); thr[tid]=keep[tid].back().sc0; } } } } } }
    std::vector<Ent> E; for(auto&v:keep) for(auto&x:v) E.push_back(x);
    std::sort(E.begin(),E.end(),[](const Ent&a,const Ent&b){return a.sc0>b.sc0;});
    printf("phase0 %.1fs kept %zu best %d/%zu\n",omp_get_wtime()-t0,E.size(),E[0].sc0,T0.size()); fflush(stdout);
    size_t k=std::min<size_t>(E.size(),80000);
    #pragma omp parallel for schedule(dynamic,16)
    for(size_t i=0;i<k;i++) E[i].scF=solveg(E[i].c,ALLT,GT,1ll<<(E[i].c.s2+1),&E[i].k1,&E[i].k2);
    E.resize(k); std::sort(E.begin(),E.end(),[](const Ent&a,const Ent&b){return a.scF>b.scF;});
    puts("\nBEST WITH THE GUARD NODES EXACT:");
    puts(" score | cst        g1 s1 r1 rule1 mask m1 g2 s2 r2 rule2 | K1        K2");
    char mb[16];
    for(int i=0;i<15;i++){ Ent&x=E[i]; Cfg&c=x.c;
        printf("%5d | %10lld %2d %2d %d %-5s %-5s %+d %2d %2d %d %-5s | %9lld %lld\n",x.scF,(long long)c.cst,
            c.g1,c.s1,c.r1,RULENAME[c.rule1],MASKNAME(c.mm,mb),c.m1,c.g2,c.s2,c.r2,RULENAME[c.rule2],
            (long long)x.k1,(long long)x.k2); }
    // baseline for reference
    Cfg cur{0x4ef145,13,23,0,0,0,0,13,20,0,0}; i64 a,b;
    printf("baseline chain, guard-constrained best: %d (K1=%lld K2=%lld)\n",solveg(cur,ALLT,GT,1ll<<21,&a,&b),(long long)a,(long long)b);
    printf("total %.1fs\n",omp_get_wtime()-t0);
    return 0; }
