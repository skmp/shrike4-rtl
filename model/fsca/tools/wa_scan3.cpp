// wa_scan3.cpp -- same family as wa_scan2 but ranked by a 24-t medium filter (no hard pair gate),
// with a deep final pass, to find the family's BEST achievable score over all 1022 t.
#include "wa_common.h"
#include <omp.h>
static Windows W;
struct Ent { Cfg c; int sc0,sc1,sc2; i64 k1,k2l,k2h; };
int main(){ build_tables(); if(!load_windows(W)){puts("no wa_jwin.txt");return 1;}
    std::vector<int> ALLT; for(int t=-512;t<=511;t++) if(W.ok[t+512]) ALLT.push_back(t);
    std::vector<int> byw=ALLT; std::sort(byw.begin(),byw.end(),[&](int a,int b){
        return (W.JH[a+512]-W.JL[a+512])<(W.JH[b+512]-W.JL[b+512]); });
    std::vector<int> T0(byw.begin(),byw.begin()+24), T1(byw.begin(),byw.begin()+200);
    std::vector<std::pair<i64,int>> csts;
    for(int j=0;j<=2;j++) for(int d=-8;d<=8;d++){ csts.push_back({((i64)(0x4ef145+d))<<j,j}); csts.push_back({-(((i64)(0x4ef145+d))<<j),j}); }
    struct Outer { i64 cst; int g1,s1,r1,rule1,m1; };
    std::vector<Outer> outs;
    for(auto&cc:csts) for(int g1=8;g1<=15;g1++) for(int e=-9;e<=-3;e++) for(int r1=0;r1<2;r1++)
        for(int rule1=0;rule1<7;rule1++) for(int m1=-1;m1<=1;m1++){ int s1=g1+17+e+cc.second;
            if(s1<4||s1>44)continue; outs.push_back({cc.first,g1,s1,r1,rule1,m1}); }
    int MM[9]={0,1,2,3,4,9,10,11,12};
    printf("outer %zu ; |T0|=%zu |T1|=%zu\n",outs.size(),T0.size(),T1.size()); fflush(stdout);
    int nth=omp_get_max_threads(); std::vector<std::vector<Ent>> keep(nth);
    double t0=omp_get_wtime(); long long tested=0; int THR=20;
    #pragma omp parallel for schedule(dynamic,64) reduction(+:tested)
    for(size_t oi=0;oi<outs.size();oi++){ Outer O=outs[oi]; int tid=omp_get_thread_num();
        int e=O.s1-O.g1-17;
        for(int mi=0;mi<9;mi++) for(int g2=8;g2<=15;g2++){ int s2=g2-e; if(s2<4||s2>44)continue;
          for(int r2=0;r2<2;r2++){ int nr2=O.cst<0?7:1;
            for(int rule2=0;rule2<nr2;rule2++){ tested++;
                Cfg c{O.cst,O.g1,O.s1,O.r1,O.rule1,MM[mi],O.m1,g2,s2,r2,rule2};
                int s=solve(c,W,T0,1ll<<(s2+1));
                if(s>=THR) keep[tid].push_back({c,s,0,0,0,0,0}); } } } }
    std::vector<Ent> E; for(auto&v:keep) for(auto&x:v) E.push_back(x);
    printf("phase0: %lld tested, %zu with T0 >= %d (%.1fs)\n",tested,E.size(),THR,omp_get_wtime()-t0); fflush(stdout);
    std::sort(E.begin(),E.end(),[](const Ent&a,const Ent&b){return a.sc0>b.sc0;});
    printf("best T0 = %d / %zu\n",E[0].sc0,T0.size());
    size_t k=std::min<size_t>(E.size(),200000);
    #pragma omp parallel for schedule(dynamic,32)
    for(size_t i=0;i<k;i++) E[i].sc1=solve(E[i].c,W,T1,1ll<<(E[i].c.s2+1));
    std::sort(E.begin(),E.begin()+k,[](const Ent&a,const Ent&b){return a.sc1>b.sc1;});
    printf("phase1 best on |T1|=%zu: %d (%.1fs)\n",T1.size(),E[0].sc1,omp_get_wtime()-t0); fflush(stdout);
    size_t k2=std::min<size_t>(k,25000);
    #pragma omp parallel for schedule(dynamic,8)
    for(size_t i=0;i<k2;i++) E[i].sc2=solve(E[i].c,W,ALLT,1ll<<(E[i].c.s2+1),&E[i].k1,&E[i].k2l,&E[i].k2h);
    std::sort(E.begin(),E.begin()+k2,[](const Ent&a,const Ent&b){return a.sc2>b.sc2;});
    puts("\n score | cst        g1 s1 r1 rule1 mask m1 g2 s2 r2 rule2 |  K1        K2 range       | T0 T1");
    char mb[16];
    for(int i=0;i<25;i++){ Ent&x=E[i]; Cfg&c=x.c;
        printf("%5d | %10lld %2d %2d %d %-5s %-5s %+d %2d %2d %d %-5s | %9lld [%lld,%lld] | %d %d\n",
            x.sc2,(long long)c.cst,c.g1,c.s1,c.r1,RULENAME[c.rule1],MASKNAME(c.mm,mb),c.m1,c.g2,c.s2,c.r2,RULENAME[c.rule2],
            (long long)x.k1,(long long)x.k2l,(long long)x.k2h,x.sc0,x.sc1); }
    printf("total %.1fs\n",omp_get_wtime()-t0);
    return 0; }
