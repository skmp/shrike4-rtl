// wa_scan1.cpp -- exhaustive structure scan, scored on a hard-t filter set, then refined.
// pass1: 34 hardest t ; pass2: 160 t ; pass3: all 1022.
#include "wa_common.h"
#include <omp.h>
static Windows W;
struct Ent { Cfg c; int sc1,sc2,sc3; i64 k1,k2l,k2h; };
int main(int argc,char**argv){ build_tables(); if(!load_windows(W)){puts("no wa_jwin.txt");return 1;}
    // ---- t sets
    std::vector<int> all; for(int t=-512;t<=511;t++) if(W.ok[t+512]) all.push_back(t);
    std::vector<int> byw=all; std::sort(byw.begin(),byw.end(),[&](int a,int b){
        return (W.JH[a+512]-W.JL[a+512])<(W.JH[b+512]-W.JL[b+512]); });
    std::vector<int> T1, T2;
    int hard[]={342,411,447,277,469,251,40,376,258,144,303,348,408};
    for(int h:hard){ T1.push_back(h); T1.push_back(-h); }
    for(int i=0;i<(int)byw.size()&&(int)T1.size()<40;i++){ int t=byw[i];
        if(std::find(T1.begin(),T1.end(),t)==T1.end()) T1.push_back(t); }
    for(int i=0;i<(int)byw.size()&&(int)T2.size()<170;i++){ int t=byw[i]; T2.push_back(t); if(std::find(T2.begin(),T2.end(),-t)==T2.end()&&W.ok[-t+512])T2.push_back(-t); }
    for(int t:T1) if(std::find(T2.begin(),T2.end(),t)==T2.end()) T2.push_back(t);
    printf("|T1|=%zu |T2|=%zu |T3|=%zu\n",T1.size(),T2.size(),all.size());
    // ---- constant list: (value, extra shift added to s1)
    std::vector<std::pair<i64,int>> csts;
    for(int d=-8;d<=8;d++) csts.push_back({0x4ef145+d,0});
    for(int j=1;j<=3;j++) for(int d=-2;d<=2;d++) csts.push_back({((i64)(0x4ef145+d))<<j,j});
    size_t ncs=csts.size(); std::vector<std::pair<i64,int>> cs2;
    for(auto&p:csts){ cs2.push_back(p); cs2.push_back({-p.first,p.second}); }
    csts=cs2; printf("constants: %zu\n",csts.size());
    // ---- enumerate
    std::vector<Cfg> cfgs;
    for(size_t ci=0;ci<csts.size();ci++) for(int g1=8;g1<=15;g1++) for(int e=-8;e<=-3;e++)
      for(int r1=0;r1<2;r1++) for(int rule1=0;rule1<7;rule1++) for(int m1=-1;m1<=1;m1++)
        for(int g2=8;g2<=15;g2++) for(int r2=0;r2<2;r2++){
            int s1=g1+17+e+csts[ci].second, s2=g2-e;
            if(s1<4||s1>40||s2<4||s2>40) continue;
            int nr2 = csts[ci].first<0 ? 7 : 1;
            for(int rule2=0;rule2<nr2;rule2++)
                cfgs.push_back({csts[ci].first,g1,s1,r1,rule1,0,m1,g2,s2,r2,rule2});
        }
    printf("structures: %zu\n",cfgs.size()); fflush(stdout);
    std::vector<int> sc1(cfgs.size(),-1);
    double t0=omp_get_wtime();
    #pragma omp parallel for schedule(dynamic,1024)
    for(size_t i=0;i<cfgs.size();i++) sc1[i]=solve(cfgs[i],W,T1,1ll<<(cfgs[i].s2+1));
    printf("pass1 done in %.1fs\n",omp_get_wtime()-t0); fflush(stdout);
    std::vector<size_t> idx; for(size_t i=0;i<cfgs.size();i++) idx.push_back(i);
    std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return sc1[a]>sc1[b];});
    printf("pass1 best=%d ; count at best=%zu\n",sc1[idx[0]],
        (size_t)std::count(sc1.begin(),sc1.end(),sc1[idx[0]]));
    size_t keep=std::min<size_t>(idx.size(),40000);
    while(keep<idx.size()&&sc1[idx[keep]]==sc1[idx[keep-1]])keep++;
    printf("pass2 on %zu\n",keep); fflush(stdout);
    std::vector<Ent> e2(keep);
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t i=0;i<keep;i++){ Cfg&c=cfgs[idx[i]]; e2[i].c=c; e2[i].sc1=sc1[idx[i]];
        e2[i].sc2=solve(c,W,T2,1ll<<(c.s2+1)); }
    std::sort(e2.begin(),e2.end(),[](const Ent&a,const Ent&b){return a.sc2>b.sc2;});
    printf("pass2 best=%d\n",e2[0].sc2); fflush(stdout);
    size_t keep3=std::min<size_t>(e2.size(),3000);
    while(keep3<e2.size()&&e2[keep3].sc2==e2[keep3-1].sc2)keep3++;
    printf("pass3 on %zu\n",keep3); fflush(stdout);
    std::vector<Ent> e3(e2.begin(),e2.begin()+keep3);
    #pragma omp parallel for schedule(dynamic,8)
    for(size_t i=0;i<e3.size();i++){ Ent&E=e3[i]; E.sc3=solve(E.c,W,all,1ll<<(E.c.s2+1),&E.k1,&E.k2l,&E.k2h); }
    std::sort(e3.begin(),e3.end(),[](const Ent&a,const Ent&b){return a.sc3>b.sc3;});
    puts("\n score | cst      g1 s1 r1 rule1  m1 g2 s2 r2 rule2 |  K1        K2 range        | T1  T2");
    for(int i=0;i<40&&i<(int)e3.size();i++){ Ent&E=e3[i]; Cfg&c=E.c;
        printf("%5d | %8llx %2d %2d %d %-5s %+d %2d %2d %d %-5s | %9lld [%lld,%lld] | %d %d\n",
            E.sc3,(long long)c.cst,c.g1,c.s1,c.r1,RULENAME[c.rule1],c.m1,c.g2,c.s2,c.r2,RULENAME[c.rule2],
            (long long)E.k1,(long long)E.k2l,(long long)E.k2h,E.sc1,E.sc2); }
    printf("total time %.1fs\n",omp_get_wtime()-t0);
    return 0; }
