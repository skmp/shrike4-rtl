// wa_cst.cpp -- which integer constant does the data actually want?  Score of the pure even model
// J = floor(a t^2 + b) with a = C/2^17 for integer C, b optimised exactly.
#include "wa_common.h"
typedef long double ld;
static Windows W;
static int cover(ld a){ std::vector<std::pair<ld,int>> ev;
    for(int t=-512;t<=511;t++){ if(!W.ok[t+512])continue; ld q=a*(ld)t*(ld)t;
        ev.push_back({(ld)W.JL[t+512]-q,1}); ev.push_back({(ld)W.JH[t+512]+1-q,-1}); }
    std::sort(ev.begin(),ev.end()); int cur=0,best=0; size_t i=0;
    while(i<ev.size()){ ld x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;}
        if(cur>best)best=cur; } return best; }
int main(){ build_tables(); if(!load_windows(W))return 1;
    puts("C (constant at 2^17)   score of J=floor(C t^2/2^17 + b)");
    for(i64 C=5173565;C<=5173582;C++) printf("  %lld %s : %d\n",(long long)C,C==0x4ef145?"(0x4EF145)":"          ",cover((ld)C/131072.0L));
    printf("\n4*pi^2*2^17 = %.2Lf ; 2*pi^2*2^18 = %.2Lf\n",4*3.14159265358979323846L*3.14159265358979323846L*131072,
        2*3.14159265358979323846L*3.14159265358979323846L*262144);
    return 0; }
