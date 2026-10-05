// we_full.cpp -- b2.cpp-style end-to-end scoring with a substitutable square-latch rule J(t).
// Reports window violations over nodes 0..31 and whether the guard nodes 0,1,2,16 stay exact.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 lat(i64 P,int s,int rule){ if(s<=0) return P<<(-s); switch(rule){case 0:return P>>s; case 1:return P>=0?(P>>s):-((-P)>>s); default:return P>=0?(P>>s):-((~P)>>s);} }
struct Rule{ const char*name; int p,a,b,r1,r2,u1,u2; i64 K1,K2; };
static i64 Jof(const Rule&R,int t){ int s1=R.b+R.p, s2=R.a+17-R.p;
    i64 d1=(i64)t<<R.b, d2=(i64)t<<R.a;
    i64 I1=lat((R.r1? arr(d1,KC):arr(KC,d1))+R.K1,s1,R.u1);
    return lat((R.r2? arr(d2,I1):arr(I1,d2))+R.K2,s2,R.u2); }
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    Rule rules[]={
      {"baseline  s1=23 s2=20 K1=2^19 K2=2^19",10,13,13,0,0,0,0,1ll<<19,1ll<<19},
      {"K2 dropped s1=23 s2=20 K1=2^19 K2=0  ",10,13,13,0,0,0,0,1ll<<19,0},
      {"we_o best  s1=23 s2=20 K1=2^19 K2=2^18 r1=r2=1",10,13,13,1,1,0,0,1ll<<19,1ll<<18},
      {"we_b best  s1=18 s2=18 b=9 a=10 K1=2^17 K2=0 r2=1 u2=sm",9,10,9,0,1,0,2,1ll<<17,0},
      {"we_b alt   s1=19 s2=20 b=9 a=13 K1=2^18 K2=0 u2=sm",10,13,9,0,0,0,2,1ll<<18,0},
      {"BEST guard-exact: I1=(arr(KC,t<<14)+2^20)>>24 ; J=(arr(I1,t<<15)+2^20)>>22",10,15,14,0,0,0,0,1ll<<20,1ll<<20},
    };
    for(auto&R:rules){
        int jsc=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 j=Jof(R,t); if(j>=JLo[t+512]&&j<=JHi[t+512])jsc++; }
        int grand=0; int pern[32]={0}; bool guard=true;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            for(int t=-512;t<=511;t++){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 T2=T.S? sm(arr(Jof(R,t),T.S)+K,U2TAB[n]):0;
                i64 s=flip?-1:1; i64 V=C0TAB[n]+s*T1-T2-s*T3;
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi){ grand++; pern[node]++; } } }
        for(int n : {0,1,2,16}) if(pern[n]) guard=false;
        printf("%-50s  jwin=%4d  violations(nodes 0..31)=%4d  guard(0,1,2,16)=%s\n",R.name,jsc,grand,guard?"EXACT":"BROKEN");
        printf("    per node:"); for(int n=0;n<32;n++) if(pern[n]) printf(" n%d:%d",n,pern[n]); puts("");
    }
    return 0; }
