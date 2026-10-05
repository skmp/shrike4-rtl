// we_win.h -- shared: build the per-t allowed-J window (JL,JH,OK) exactly as b12/b14 do.
#pragma once
#include "base2.h"
#include <vector>
static Fsca WF;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(WF.srz[a],WF.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JLo[1024],JHi[1024]; static char JOK[1024];
// per-(t,node) T2 window, for diagnostics
struct Cons{ int node,n,flip; i64 lo,hi; i64 S; int u; };
static std::vector<Cons> CONS[1024];
static void build_windows(){
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            CONS[t+512].push_back({node,n,flip,t2lo,t2hi,T.S,u});
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLo[t+512]=gl;JHi[t+512]=gh;JOK[t+512]= gl<=gh; }
}
static inline int score_rule(i64 (*Jf)(int t)){ int s=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 j=Jf(t); if(j>=JLo[t+512]&&j<=JHi[t+512])s++; } return s; }
