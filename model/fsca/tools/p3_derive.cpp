// p3_derive.cpp -- is the FSRRA-style C2_n a DERIVED rule?  For the best (g,u,SQK), find for each node
// the FULL set of C2 achieving the minimum violations, then ask whether a single constant C exists with
// C2_n = RN(sin(n*pi/32) * C) inside that set for EVERY node.  Also scans SQK finely.
#include "base2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
static Fsca F;
static const int64_t C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct A { int m,t; int64_t rest,Vlo,Vhi; };
static std::vector<A> AS;
static const int64_t CMAX=(1LL<<28)-1;
// best C2 set per node: returns min violations and the list of maximal intervals achieving it
static int nodeBest(int n,int g,int u,int64_t SQK,std::vector<std::pair<int64_t,int64_t>>&outset){
    std::vector<std::pair<int64_t,int>> ev; int nt=0;
    for(const A&x:AS){ if(x.m!=n||x.t==0) continue;
        int64_t dx=(int64_t)x.t<<13; int64_t sq=((arr(dx,dx)+K)>>(26+g))+SQK; if(sq<=0) continue; nt++;
        int64_t T2lo=x.rest-x.Vhi,T2hi=x.rest-x.Vlo;
        auto t2=[&](int64_t C){ return sm(arr(sq,C)+K,u); };
        if(t2(CMAX)<T2lo||t2(0)>T2hi) continue;
        int64_t a=0,b=CMAX; while(a<b){int64_t m2=(a+b)/2; if(t2(m2)>=T2lo)b=m2; else a=m2+1;} int64_t L=a;
        a=0;b=CMAX; while(a<b){int64_t m2=(a+b+1)/2; if(t2(m2)<=T2hi)a=m2; else b=m2-1;} int64_t R=a;
        if(L<=R){ ev.push_back({L,+1}); ev.push_back({R+1,-1}); }
    }
    std::sort(ev.begin(),ev.end());
    int cur=0,mx=0; for(auto&e:ev){cur+=e.second; mx=std::max(mx,cur);} 
    outset.clear(); cur=0; int64_t st=0; bool in=false;
    for(size_t i=0;i<ev.size();i++){ int prev=cur; cur+=ev[i].second;
        if(prev<mx&&cur==mx){ st=ev[i].first; in=true; }
        else if(in&&prev==mx&&cur<mx){ outset.push_back({st,ev[i].first-1}); in=false; } }
    return nt-mx;
}
int main(){ if(!load_fsca(F)) return 1; build_tables();
    for(int a=0;a<32768;a++){ int q=(int)((a+512u)>>10),m=q&63,t=a-1024*q,flip=0; if(t<-512)t+=65536;
        if(m>=17){m=32-m;flip=1;} if(m<0||m>16) continue;
        int64_t s=flip?-1:1,dx=(int64_t)t<<13; const Tab&T=TB[m];
        int64_t T1=T.C1f?sm(arr(T.C1f,dx)+K,23-T.e1):0;
        int64_t A3=((arr(((int64_t)t*t)<<3,dx)+K)>>20)>>3;
        int64_t T3=T.c3?sm(arr(T.c3<<5,A3)+K,26):0;
        Win w=decode(F.srz[a],F.srn[a]); int64_t lo=w.sign>0?w.lo:-w.hi,hi=w.sign>0?w.hi:-w.lo;
        AS.push_back({m,t,C0T[m]+s*T1-s*T3,lo,hi}); }
    printf("SQK scan at g=0 (FSRRA's compensation constant), u in 27..30:\n");
    int bv=1<<30,bu=0; int64_t bk=0;
    for(int u=27;u<=30;u++) for(int64_t SQK=-256;SQK<=256;SQK+=8){
        int tot=0; std::vector<std::pair<int64_t,int64_t>> s;
        for(int n=0;n<17;n++) tot+=nodeBest(n,0,u,SQK,s);
        if(tot<bv){ bv=tot;bu=u;bk=SQK; printf("  u=%d SQK=%+4lld -> %d violations\n",u,(long long)SQK,tot); }
    }
    printf("\nbest: %d violations at g=0 u=%d SQK=%lld (baseline 147)\n\n",bv,bu,(long long)bk);
    printf("PER-NODE allowed C2 sets at that config, and the DERIVED candidate RN(sin(n*pi/32)*C):\n");
    std::vector<std::vector<std::pair<int64_t,int64_t>>> sets(17);
    for(int n=0;n<17;n++){ int v=nodeBest(n,0,bu,bk,sets[n]);
        printf("  n=%2d viol=%3d  C2 set:",n,v);
        for(size_t i=0;i<sets[n].size()&&i<3;i++) printf(" [%lld,%lld]",(long long)sets[n][i].first,(long long)sets[n][i].second);
        printf("\n"); }
    // is there ONE C with RN(sin*C) in every node's set?
    printf("\nsearching for a single constant C with RN(sin(n*pi/32)*C) in every node's allowed set...\n");
    long double S[17]; for(int n=0;n<17;n++) S[n]=sinl((long double)n*3.14159265358979323846L/32.0L);
    int64_t found=0; int bestnodes=-1; int64_t bestC=0;
    for(int64_t C=82000000;C<=83600000;C++){
        int ok=0; for(int n=0;n<17;n++){ int64_t v=(int64_t)llroundl(S[n]*(long double)C);
            bool in=false; for(auto&iv:sets[n]) if(v>=iv.first&&v<=iv.second){in=true;break;}
            if(n==0) in=true; if(in) ok++; }
        if(ok>bestnodes){ bestnodes=ok; bestC=C; }
        if(ok==17){ found=C; break; }
    }
    if(found) printf("  *** FOUND C = %lld : the derived rule RN(sin(n*pi/32)*C) fits ALL 17 nodes ***\n",(long long)found);
    else printf("  best C = %lld fits %d of 17 nodes (no single C fits all)\n",(long long)bestC,bestnodes);
    printf("  for reference pi^2 * 2^23 = %.1f ; 4*pi^2*2^21 = %.1f\n",9.869604401089358*8388608.0,4*9.869604401089358*2097152.0);
    return 0; }
