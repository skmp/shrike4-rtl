// wc_core.h -- shared precomputation and scoring for the square-path enumeration (wc_ family).
//   V(node,t) = C0_n + L(node,t) - T2(node,t)      L = s*(T1 - T3), s = -1 for nodes 17..31
// Window data is RAW: all 31 node instances, 1024 offsets each.  C0_n is a stored 26-bit table value
// shared by node n and node 32-n; it is refitted per candidate inside the b2 range so that scores are
// directly comparable with the 148 baseline.
#pragma once
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca WF;
static i64 WLO[32][1024], WHI[32][1024];     // V window
static i64 LT [32][1024];                    // linear+cubic part, without C0
static i64 C0LO[17], C0HI[17];               // C0 search range per table entry (b2's: t=0 window +-3)
static i64 C0B [17];                         // b2's chosen C0 (baseline)
static const i64 C0TABB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void wwin(int a,i64&lo,i64&hi,bool&ex){Win w=decode(WF.srz[a],WF.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// per-table coefficient variants for the square term
struct Coef { i64 v; int ub; };              // value and the final unit column
static Coef CF[3][17];                       // 0 = normalised sine field S (ub = 30+k)
                                             // 1 = stored RN(sin*pi^2*2^(19+j)) normalised, ub = 24+j
                                             // 2 = stored RN(sin*KC*2^(1+j)) normalised (economised), ub = 25+j
static void wc_init(){
    if(!load_fsca(WF)){ fprintf(stderr,"dump load failed\n"); exit(1);} build_tables();
    for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            LT[node][t+512]=s*T1-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; wwin(a,lo,hi,ex);
            WLO[node][t+512]=lo; WHI[node][t+512]=hi; } }
    for(int n=0;n<=16;n++){ i64 lo,hi;bool ex; wwin(n*1024,lo,hi,ex); C0LO[n]=lo-3; C0HI[n]=hi+3; C0B[n]=C0TABB[n]; }
    for(int n=0;n<=16;n++){
        CF[0][n]={TB[n].S, TB[n].u2};
        i128 a=PI_()*n/32, s=sin_(a);
        if(n==0){ CF[1][n]={0,24}; CF[2][n]={0,25}; continue; }
        i128 p2=PI_()*PI_()/QONE;                       // pi^2 in Q60
        { i128 raw=s*p2/QONE;                           // sin*pi^2 in Q60
          int j=19; while(rn_scale(raw,j) >= (1ll<<23)) j--; while(rn_scale(raw,j) < (1ll<<22)) j++;
          CF[1][n]={rn_scale(raw,j), 5+j}; }            // C2/2^u = sin*pi^2/2^5 -> u = 5+j
        { i128 raw=s*(i128)KC*2;                        // sin*KC*2 in Q60 (economised)
          int j=0; while(rn_scale(raw,j) >= (1ll<<23)) j--; while(rn_scale(raw,j) < (1ll<<22)) j++;
          CF[2][n]={rn_scale(raw,j), 25+j}; }           // KC*2 = pi^2*2^20*econ -> u = 25+j
    }
}
// latch rules
static inline i64 wlatch(i64 P,int s,int rule){
    switch(rule){ case 0: return P>>s;                                  // floor
                  case 1: return P>=0? (P>>s) : -((-P)>>s);             // truncate toward zero
                  default: return P>=0? (P>>s) : -((~P)>>s); } }        // one's complement magnitude
// ---- full score: T2 supplied by a callback per (node,t).  Returns total violations; fills per-node.
template<class FT2> static int wc_score(FT2&& t2, int* per31=nullptr, int* guard=nullptr){
    static thread_local i64 T2[32][1024];
    for(int node=0;node<32;node++) for(int t=-512;t<=511;t++) T2[node][t+512]=t2(node,t);
    int tot=0; if(guard)*guard=0;
    for(int n=0;n<=16;n++){ int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        int bv=1<<30, bp[2]={0,0};
        for(i64 c=C0LO[n];c<=C0HI[n];c++){ int v=0,p[2]={0,0};
            for(int i=0;i<nn;i++){ int m=nodes[i];
                for(int t=-512;t<=511;t++){ i64 V=c+LT[m][t+512]-T2[m][t+512];
                    if(V<WLO[m][t+512]||V>WHI[m][t+512]){v++;p[i]++;} } }
            if(v<bv){bv=v;bp[0]=p[0];bp[1]=p[1];} }
        tot+=bv; if(per31){ per31[nodes[0]]=bp[0]; if(nn==2) per31[nodes[1]]=bp[1]; }
        if(guard && (n==0||n==1||n==2||n==16) && bv) *guard+=bv; }
    return tot; }
