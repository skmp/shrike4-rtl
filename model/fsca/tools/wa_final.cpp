// wa_final.cpp -- clean final numbers for named structures: unconstrained best (K1,K2),
// guard-constrained best (nodes 1,2,16,30,31 exact), and the missed-t list.
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
// brute force over K1 breakpoints, exact over K2 by sweep, with an optional guard requirement
static void report(const char*nm,Cfg c,bool guarded){
    i64 S1=1ll<<c.s1; std::vector<i64> bps; bps.push_back(0);
    for(int t=-512;t<=511;t++){ i64 P1=arrole(c.cst,(i64)t<<c.g1,c.r1);
        i64 vA=latchv(P1,0,c.s1,c.rule1), vB=latchv(P1,S1-1,c.s1,c.rule1);
        if(vA==vB) continue;
        i64 a=0,b=S1-1; while(a<b){ i64 m=a+((b-a)>>1); if(latchv(P1,m,c.s1,c.rule1)!=vA)b=m; else a=m+1; }
        bps.push_back(a); }
    std::sort(bps.begin(),bps.end()); bps.erase(std::unique(bps.begin(),bps.end()),bps.end());
    int best=-1; i64 bk1=0,bk2=0;
    for(i64 k1 : bps){
        // guard range
        i64 gl=-(1ll<<40), gh=(1ll<<40); bool bad=false;
        if(guarded) for(int t=-512;t<=511;t++){ if(!G.ok[t+512])continue;
            i64 I1=applymask(latchv(arrole(c.cst,(i64)t<<c.g1,c.r1),k1,c.s1,c.rule1)+c.m1,c.mm);
            i64 P2=arrole(I1,(i64)t<<c.g2,c.r2); i64 a,b;
            if(!k2_interval(P2,c.s2,c.rule2,c.cst>=0?1:-1,G.JL[t+512],G.JH[t+512],1ll<<(c.s2+2),a,b)){bad=true;break;}
            if(a>gl)gl=a; if(b<gh)gh=b; if(gl>gh){bad=true;break;} }
        if(bad) continue;
        std::vector<std::pair<i64,int>> ev;
        for(int t=-512;t<=511;t++){ if(!W.ok[t+512])continue;
            i64 I1=applymask(latchv(arrole(c.cst,(i64)t<<c.g1,c.r1),k1,c.s1,c.rule1)+c.m1,c.mm);
            i64 P2=arrole(I1,(i64)t<<c.g2,c.r2); i64 a,b;
            if(!k2_interval(P2,c.s2,c.rule2,c.cst>=0?1:-1,W.JL[t+512],W.JH[t+512],1ll<<(c.s2+2),a,b))continue;
            a=std::max(a,gl); b=std::min(b,gh); if(a>b)continue;
            ev.push_back({a,1}); ev.push_back({b+1,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0; size_t i=0;
        while(i<ev.size()){ i64 x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;}
            if(cur>best){best=cur;bk1=k1;bk2=x;} } }
    printf("%-42s %s: %d/1022  K1=%lld K2=%lld\n",nm,guarded?"[guarded]":"[free]   ",best,(long long)bk1,(long long)bk2);
    if(best>=0) printf("     direct re-evaluation: %d\n",score_exact(c,W,bk1,bk2));
}
int main(){ if(!load_fsca(F))return 1; build_tables(); if(!load_windows(W))return 1; build_guard();
    Cfg base{0x4ef145,13,23,0,0,0,0,13,20,0,0};
    Cfg b944{-5173574,10,19,1,3,1,1,13,21,1,6};
    report("baseline chain (KC,13,23,floor|13,20,floor)",base,false);
    report("baseline chain (KC,13,23,floor|13,20,floor)",base,true);
    report("family best (wa_scan2)",b944,false);
    report("family best (wa_scan2)",b944,true);
    return 0; }
