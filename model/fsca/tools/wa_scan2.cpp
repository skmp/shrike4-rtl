// wa_scan2.cpp -- full family scan WITH I1 width/mask variants, pre-filtered on the tightest
// constraint pair t=+-342 (J(342) pinned to one value, J(-342) to two, D in [6,7]).
#include "wa_common.h"
#include <omp.h>
static Windows W;
struct Ent { Cfg c; int sc1,sc2,sc3; i64 k1,k2l,k2h; };
static std::vector<int> T1,T2,ALLT;
int main(int argc,char**argv){ build_tables(); if(!load_windows(W)){puts("no wa_jwin.txt");return 1;}
    for(int t=-512;t<=511;t++) if(W.ok[t+512]) ALLT.push_back(t);
    std::vector<int> byw=ALLT; std::sort(byw.begin(),byw.end(),[&](int a,int b){
        return (W.JH[a+512]-W.JL[a+512])<(W.JH[b+512]-W.JL[b+512]); });
    int hard[]={342,411,447,277,469,251,40,376,258,144,303,348,408};
    for(int h:hard){ T1.push_back(h); T1.push_back(-h); }
    for(int i=0;i<(int)byw.size()&&(int)T1.size()<40;i++){ int t=byw[i]; if(std::find(T1.begin(),T1.end(),t)==T1.end())T1.push_back(t); }
    for(int i=0;i<(int)byw.size()&&(int)T2.size()<170;i++){ int t=byw[i]; T2.push_back(t); if(W.ok[-t+512]&&std::find(T2.begin(),T2.end(),-t)==T2.end())T2.push_back(-t); }
    for(int t:T1) if(std::find(T2.begin(),T2.end(),t)==T2.end()) T2.push_back(t);
    // ---- constants (value, extra shift on s1)
    std::vector<std::pair<i64,int>> csts;
    for(int j=0;j<=2;j++) for(int d=-8;d<=8;d++){ csts.push_back({((i64)(0x4ef145+d))<<j,j}); csts.push_back({-(((i64)(0x4ef145+d))<<j),j}); }
    printf("constants %zu\n",csts.size());
    // ---- outer enumeration
    struct Outer { i64 cst; int g1,s1,r1,rule1,m1; };
    std::vector<Outer> outs;
    for(auto&cc:csts) for(int g1=8;g1<=15;g1++) for(int e=-9;e<=-3;e++) for(int r1=0;r1<2;r1++)
        for(int rule1=0;rule1<7;rule1++) for(int m1=-1;m1<=1;m1++){
            int s1=g1+17+e+cc.second; if(s1<4||s1>44) continue;
            outs.push_back({cc.first,g1,s1,r1,rule1,m1}); }
    printf("outer combos %zu\n",outs.size()); fflush(stdout);
    int MM[9]={0,1,2,3,4,9,10,11,12};
    std::vector<std::vector<Cfg>> found(omp_get_max_threads());
    double t0=omp_get_wtime(); long long tested=0;
    #pragma omp parallel for schedule(dynamic,256) reduction(+:tested)
    for(size_t oi=0;oi<outs.size();oi++){ Outer O=outs[oi]; int tid=omp_get_thread_num();
        i64 S1=1ll<<O.s1; int jsign=O.cst>=0?1:-1;
        i64 vv[2][2];
        for(int k=0;k<2;k++){ int t=k?-342:342; i64 P1=arrole(O.cst,(i64)t<<O.g1,O.r1);
            vv[k][0]=latchv(P1,0,O.s1,O.rule1)+O.m1; vv[k][1]=latchv(P1,S1-1,O.s1,O.rule1)+O.m1; }
        int e = O.s1-O.g1-17;                       // note: includes the constant's extra shift
        for(int mi=0;mi<9;mi++){ int mm=MM[mi];
            i64 mv[2][2]; for(int k=0;k<2;k++)for(int c=0;c<2;c++) mv[k][c]=applymask(vv[k][c],mm);
            for(int g2=8;g2<=15;g2++){ int s2=g2-e; if(s2<4||s2>44) continue;
              for(int r2=0;r2<2;r2++){
                i64 P2[2][2]; for(int k=0;k<2;k++){ int t=k?-342:342;
                    for(int c=0;c<2;c++) P2[k][c]=arrole(mv[k][c],(i64)t<<g2,r2); }
                int nr2 = O.cst<0?7:1;
                for(int rule2=0;rule2<nr2;rule2++){ tested++;
                    bool okpair=false;
                    for(int c0=0;c0<2&&!okpair;c0++) for(int c1=0;c1<2&&!okpair;c1++){
                        i64 a0,b0,a1,b1;
                        if(!k2_interval(P2[0][c0],s2,rule2,jsign,W.JL[342+512],W.JH[342+512],1ll<<(s2+1),a0,b0))continue;
                        if(!k2_interval(P2[1][c1],s2,rule2,jsign,W.JL[-342+512],W.JH[-342+512],1ll<<(s2+1),a1,b1))continue;
                        if(std::max(a0,a1)<=std::min(b0,b1)) okpair=true; }
                    if(okpair) found[tid].push_back({O.cst,O.g1,O.s1,O.r1,O.rule1,mm,O.m1,g2,s2,r2,rule2});
                } } } } }
    std::vector<Cfg> surv; for(auto&v:found) for(auto&c:v) surv.push_back(c);
    printf("phase0: %lld structures tested, %zu survive the t=+-342 pair (%.1fs)\n",tested,surv.size(),omp_get_wtime()-t0);
    fflush(stdout);
    if(surv.empty()){ puts("FAMILY DEAD: no two-multiply chain in this space can satisfy t=+-342."); return 0; }
    std::vector<int> s1v(surv.size());
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t i=0;i<surv.size();i++) s1v[i]=solve(surv[i],W,T1,1ll<<(surv[i].s2+1));
    std::vector<size_t> idx(surv.size()); for(size_t i=0;i<idx.size();i++)idx[i]=i;
    std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return s1v[a]>s1v[b];});
    printf("phase1 best on |T1|=%zu: %d\n",T1.size(),s1v[idx[0]]); fflush(stdout);
    size_t keep=std::min<size_t>(idx.size(),20000); while(keep<idx.size()&&s1v[idx[keep]]==s1v[idx[keep-1]])keep++;
    std::vector<Ent> e2(keep);
    #pragma omp parallel for schedule(dynamic,32)
    for(size_t i=0;i<keep;i++){ e2[i].c=surv[idx[i]]; e2[i].sc1=s1v[idx[i]]; e2[i].sc2=solve(e2[i].c,W,T2,1ll<<(e2[i].c.s2+1)); }
    std::sort(e2.begin(),e2.end(),[](const Ent&a,const Ent&b){return a.sc2>b.sc2;});
    printf("phase2 best on |T2|=%zu: %d\n",T2.size(),e2[0].sc2); fflush(stdout);
    size_t k3=std::min<size_t>(e2.size(),2000); while(k3<e2.size()&&e2[k3].sc2==e2[k3-1].sc2)k3++;
    std::vector<Ent> e3(e2.begin(),e2.begin()+k3);
    #pragma omp parallel for schedule(dynamic,8)
    for(size_t i=0;i<e3.size();i++) e3[i].sc3=solve(e3[i].c,W,ALLT,1ll<<(e3[i].c.s2+1),&e3[i].k1,&e3[i].k2l,&e3[i].k2h);
    std::sort(e3.begin(),e3.end(),[](const Ent&a,const Ent&b){return a.sc3>b.sc3;});
    puts("\n score | cst        g1 s1 r1 rule1 mask m1 g2 s2 r2 rule2 |  K1        K2 range       | T1 T2");
    char mb[16];
    for(int i=0;i<30&&i<(int)e3.size();i++){ Ent&E=e3[i]; Cfg&c=E.c;
        printf("%5d | %10lld %2d %2d %d %-5s %-5s %+d %2d %2d %d %-5s | %9lld [%lld,%lld] | %d %d\n",
            E.sc3,(long long)c.cst,c.g1,c.s1,c.r1,RULENAME[c.rule1],MASKNAME(c.mm,mb),c.m1,c.g2,c.s2,c.r2,RULENAME[c.rule2],
            (long long)E.k1,(long long)E.k2l,(long long)E.k2h,E.sc1,E.sc2); }
    printf("total %.1fs\n",omp_get_wtime()-t0);
    return 0; }
