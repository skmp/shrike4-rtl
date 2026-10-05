// wa_scan4.cpp -- family scan ranked by a stratified sample, reporting BOTH the best full-window
// score and the best score among structures that keep the guard nodes (1,2,16,30,31) exact.
#include "wa_common.h"
#include <omp.h>
#include <queue>
static Fsca F; static Windows W; static Windows G;     // G = windows from the guard nodes only
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static void build_guard(){ int gn[5]={1,2,16,30,31}; G.nfeas=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int gi=0;gi<5;gi++){ int node=gn[gi]; int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        G.JL[t+512]=gl;G.JH[t+512]=gh;G.ok[t+512]=gl<=gh;G.nfeas+=G.ok[t+512]; } }
struct Ent { Cfg c; int sc0,scF,scG; i64 k1,k2l,k2h; };
int main(){ if(!load_fsca(F))return 1; build_tables(); if(!load_windows(W))return 1; build_guard();
    printf("guard windows feasible: %d/1024 ; mean width %.1f\n",G.nfeas,[&]{double s=0;int n=0;
        for(int i=0;i<1024;i++) if(G.ok[i]){s+=G.JH[i]-G.JL[i];n++;} return s/n;}());
    std::vector<int> ALLT; for(int t=-512;t<=511;t++) if(W.ok[t+512]) ALLT.push_back(t);
    std::vector<int> T0; for(int t=-496;t<=496;t+=31) if(W.ok[t+512]) T0.push_back(t);
    printf("|T0|=%zu\n",T0.size());
    std::vector<std::pair<i64,int>> csts;
    for(int j=0;j<=2;j++) for(int d=-8;d<=8;d++){ csts.push_back({((i64)(0x4ef145+d))<<j,j}); csts.push_back({-(((i64)(0x4ef145+d))<<j),j}); }
    struct Outer { i64 cst; int g1,s1,r1,rule1,m1; };
    std::vector<Outer> outs;
    for(auto&cc:csts) for(int g1=8;g1<=15;g1++) for(int e=-9;e<=-3;e++) for(int r1=0;r1<2;r1++)
        for(int rule1=0;rule1<7;rule1++) for(int m1=-1;m1<=1;m1++){ int s1=g1+17+e+cc.second;
            if(s1<4||s1>44)continue; outs.push_back({cc.first,g1,s1,r1,rule1,m1}); }
    int MM[9]={0,1,2,3,4,9,10,11,12};
    int nth=omp_get_max_threads(); std::vector<std::vector<Ent>> heap(nth);
    const size_t CAP=30000; std::vector<int> thr(nth,0);
    double t0=omp_get_wtime();
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t oi=0;oi<outs.size();oi++){ Outer O=outs[oi]; int tid=omp_get_thread_num(); int e=O.s1-O.g1-17;
        for(int mi=0;mi<9;mi++) for(int g2=8;g2<=15;g2++){ int s2=g2-e; if(s2<4||s2>44)continue;
          for(int r2=0;r2<2;r2++){ int nr2=O.cst<0?7:1;
            for(int rule2=0;rule2<nr2;rule2++){
                Cfg c{O.cst,O.g1,O.s1,O.r1,O.rule1,MM[mi],O.m1,g2,s2,r2,rule2};
                int s=solve(c,W,T0,1ll<<(s2+1));
                if(s>=thr[tid]){ heap[tid].push_back({c,s,0,0,0,0,0});
                    if(heap[tid].size()>CAP*2){ std::nth_element(heap[tid].begin(),heap[tid].begin()+CAP,heap[tid].end(),
                            [](const Ent&a,const Ent&b){return a.sc0>b.sc0;});
                        heap[tid].resize(CAP); thr[tid]=heap[tid].back().sc0; } } } } } }
    std::vector<Ent> E; for(auto&v:heap) for(auto&x:v) E.push_back(x);
    std::sort(E.begin(),E.end(),[](const Ent&a,const Ent&b){return a.sc0>b.sc0;});
    printf("phase0 %.1fs: kept %zu, best T0 = %d/%zu\n",omp_get_wtime()-t0,E.size(),E[0].sc0,T0.size()); fflush(stdout);
    size_t k=std::min<size_t>(E.size(),120000);
    #pragma omp parallel for schedule(dynamic,16)
    for(size_t i=0;i<k;i++){ E[i].scF=solve(E[i].c,W,ALLT,1ll<<(E[i].c.s2+1),&E[i].k1,&E[i].k2l,&E[i].k2h);
        // guard compliance at the SAME (K1,K2) that maximises the full score
        int g=0; for(int t=-512;t<=511;t++){ if(!G.ok[t+512])continue; i64 J=Jof(E[i].c,E[i].k1,E[i].k2l,t);
            if(J>=G.JL[t+512]&&J<=G.JH[t+512])g++; }
        E[i].scG=g; }
    E.resize(k);
    char mb[16];
    std::sort(E.begin(),E.end(),[](const Ent&a,const Ent&b){return a.scF>b.scF;});
    puts("\nBEST BY FULL WINDOW SCORE");
    puts(" score guard | cst        g1 s1 r1 rule1 mask m1 g2 s2 r2 rule2 |  K1        K2");
    for(int i=0;i<12;i++){ Ent&x=E[i]; Cfg&c=x.c;
        printf("%5d %5d | %10lld %2d %2d %d %-5s %-5s %+d %2d %2d %d %-5s | %9lld %lld\n",
            x.scF,x.scG,(long long)c.cst,c.g1,c.s1,c.r1,RULENAME[c.rule1],MASKNAME(c.mm,mb),c.m1,c.g2,c.s2,c.r2,
            RULENAME[c.rule2],(long long)x.k1,(long long)x.k2l); }
    std::sort(E.begin(),E.end(),[](const Ent&a,const Ent&b){ if(a.scG!=b.scG)return a.scG>b.scG; return a.scF>b.scF;});
    puts("\nBEST WITH THE GUARD NODES INTACT (guard must be 1024)");
    for(int i=0;i<12;i++){ Ent&x=E[i]; Cfg&c=x.c;
        printf("%5d %5d | %10lld %2d %2d %d %-5s %-5s %+d %2d %2d %d %-5s | %9lld %lld\n",
            x.scF,x.scG,(long long)c.cst,c.g1,c.s1,c.r1,RULENAME[c.rule1],MASKNAME(c.mm,mb),c.m1,c.g2,c.s2,c.r2,
            RULENAME[c.rule2],(long long)x.k1,(long long)x.k2l); }
    printf("total %.1fs\n",omp_get_wtime()-t0);
    return 0; }
