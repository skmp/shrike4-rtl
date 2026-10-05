// wa_sanity.cpp -- validate the framework against the known baseline, and prove/disprove the
// granularity bound on the required asymmetry at |t|=342.
#include "wa_common.h"
static Windows W;
int main(){ build_tables(); if(!load_windows(W)){puts("no wa_jwin.txt");return 1;}
    printf("feasible t: %d\n",W.nfeas);
    Cfg cur{0x4ef145,13,23,0,0,0,0,13,20,0,0};
    printf("current chain, K1=K2=2^19          : %d\n",score_exact(cur,W,1ll<<19,1ll<<19));
    printf("current chain, K1=2^19 K2=0        : %d\n",score_exact(cur,W,1ll<<19,0));
    std::vector<int> all; for(int t=-512;t<=511;t++) if(W.ok[t+512]) all.push_back(t);
    i64 k1,k2l,k2h; int b=solve(cur,W,all,1ll<<21,&k1,&k2l,&k2h);
    printf("current structure, BEST K1,K2 over all: %d  (K1=%lld K2 in [%lld,%lld])\n",b,(long long)k1,(long long)k2l,(long long)k2h);
    printf("   verify by direct eval: %d\n",score_exact(cur,W,k1,k2l));
    // exhaustive: what J(+-342) pairs are reachable for e=-7..-4 with the plain floor chain?
    puts("\nreachable (J(342), J(-342)) for the plain floor chain, all K1 in [0,2^s1), K2 in [-2^s2,2^s2]:");
    printf("required: J(342) in [%lld,%lld], J(-342) in [%lld,%lld]  => D=J(-342)-J(342) in [6,7]\n",
        (long long)W.JL[342+512],(long long)W.JH[342+512],(long long)W.JL[-342+512],(long long)W.JH[-342+512]);
    for(int e=-8;e<=-3;e++){ int g1=13,g2=13,s1=g1+17+e,s2=g2-e; if(s1<1||s2<1)continue;
        Cfg c{0x4ef145,g1,s1,0,0,0,0,g2,s2,0,0};
        std::vector<int> TT{342,-342}; i64 a,b2,c2; int sc=solve(c,W,TT,1ll<<(s2+1),&a,&b2,&c2);
        // also the max D achievable
        i64 S1=1ll<<s1; i64 dmin=1ll<<40,dmax=-(1ll<<40);
        for(int i=0;i<2;i++)for(int j=0;j<2;j++){ // I1 choices
            i64 P1p=arr(c.cst,(i64)342<<g1), P1m=arr(c.cst,(i64)-342<<g1);
            i64 vp=latchv(P1p,i?S1-1:0,s1,0), vm=latchv(P1m,j?S1-1:0,s1,0);
            i64 P2p=arr(vp,(i64)342<<g2), P2m=arr(vm,(i64)-342<<g2);
            i64 d=(P2m-P2p)>>s2; if(d<dmin)dmin=d; if(d>dmax)dmax=d; }
        printf("  e=%d s1=%d s2=%d: joint-feasible count at +-342 = %d ; (P2(-t)-P2(t))>>s2 in [%lld,%lld]\n",
            e,s1,s2,sc,(long long)dmin,(long long)dmax);
    }
    return 0; }
