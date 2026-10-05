// wa_verify.cpp -- verify a named structure by direct evaluation and list the t it misses.
#include "wa_common.h"
static Windows W;
static void rep(const char*name,Cfg c,i64 K1,i64 K2){
    int sc=score_exact(c,W,K1,K2); std::vector<int> miss;
    for(int t=-512;t<=511;t++){ if(!W.ok[t+512])continue; i64 J=Jof(c,K1,K2,t);
        if(J<W.JL[t+512]||J>W.JH[t+512]) miss.push_back(t); }
    printf("%s: %d/1022  (misses %zu)\n",name,sc,miss.size());
    printf("  missed t:"); for(size_t i=0;i<miss.size();i++){ if(i&&i%22==0)printf("\n           "); printf(" %d",miss[i]); } puts("");
    printf("  J(342)=%lld [need %lld..%lld]  J(-342)=%lld [need %lld..%lld]  D=%lld\n",
        (long long)Jof(c,K1,K2,342),(long long)W.JL[342+512],(long long)W.JH[342+512],
        (long long)Jof(c,K1,K2,-342),(long long)W.JL[-342+512],(long long)W.JH[-342+512],
        (long long)(Jof(c,K1,K2,-342)-Jof(c,K1,K2,342)));
    printf("  J(376)=%lld/%lld [need %lld]  J(40)=%lld/%lld [need %lld]\n",
        (long long)Jof(c,K1,K2,376),(long long)Jof(c,K1,K2,-376),(long long)W.JL[376+512],
        (long long)Jof(c,K1,K2,40),(long long)Jof(c,K1,K2,-40),(long long)W.JL[40+512]);
}
int main(){ build_tables(); if(!load_windows(W))return 1;
    rep("baseline chain K1=K2=2^19", Cfg{0x4ef145,13,23,0,0,0,0,13,20,0,0},1ll<<19,1ll<<19);
    rep("baseline chain best K1,K2", Cfg{0x4ef145,13,23,0,0,0,0,13,20,0,0},589824,0);
    rep("wa_scan2 best (944)",       Cfg{-5173574,10,19,1,3,1,1,13,21,1,6},0,-786431);
    return 0; }
