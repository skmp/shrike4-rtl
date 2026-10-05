// wv_asym_win.h -- INDEPENDENT re-derivation of the per-t feasible square-latch set.
// Written from scratch for adversarial verification; does NOT include we_win.h.
// No monotonicity assumption: for each t the set of latch values J that satisfy ALL
// node instances is found by direct enumeration around the ideal value.
#pragma once
#include "base2.h"
#include <vector>
#include <algorithm>

static Fsca WVF;
// baseline table fit reproduced by running b2.cpp in this environment (printed 2026-09-17):
static const i64 WV_C0[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                            47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int WV_U2[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

struct DWin { i64 lo, hi; };
static inline DWin wv_dumpwin(int a){ Win w=decode(WVF.srz[a],WVF.srn[a]);
    return w.sign>0 ? DWin{w.lo,w.hi} : DWin{-w.hi,-w.lo}; }

// non-square part of V for node instance `node` at offset t (C0 + s*T1 - s*T3)
static inline i64 wv_base(int node,int t){ int flip=node>=17, n=flip?32-node:node; const Tab&T=TB[n];
    i64 dx=(i64)t<<13, s=flip?-1:1;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
    i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    return WV_C0[n]+s*T1-s*T3; }

// T2 from a latch value J for node instance `node`, with the latch taken at shift (20 - extra)
// i.e. extra=0 reproduces u2=30+k (jwin.txt scale), extra=2 is the shift-18 scale of jwin18.txt.
static inline i64 wv_T2(int node,i64 J,int extra){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n];
    if(!T.S) return 0; return sm(arr(J,T.S)+K, WV_U2[n]+extra); }

// feasible latch sets
static std::vector<i64> WV_FEAS[1024];       // sorted feasible J values per t
static i64 WV_LO[1024], WV_HI[1024]; static char WV_OK[1024], WV_CONTIG[1024];
static void wv_build(int extra, i64 margin=600){
    for(int t=-512;t<=511;t++){
        i64 Jc = (KC*(i64)t*(i64)t) >> (17-extra);
        i64 j0 = std::max((i64)0, Jc-margin), j1 = Jc+margin;
        std::vector<i64>& F = WV_FEAS[t+512]; F.clear();
        // precompute the per-node allowed T2 interval
        int nodes[32]; i64 tlo[32],thi[32]; int cnt=0;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S) continue;
            int a=(node*1024+t)&0xffff; DWin w=wv_dumpwin(a); i64 b=wv_base(node,t);
            nodes[cnt]=node; tlo[cnt]=b-w.hi; thi[cnt]=b-w.lo; cnt++; }
        for(i64 J=j0;J<=j1;J++){ bool ok=true;
            for(int i=0;i<cnt&&ok;i++){ i64 v=wv_T2(nodes[i],J,extra); if(v<tlo[i]||v>thi[i]) ok=false; }
            if(ok) F.push_back(J); }
        WV_OK[t+512]= !F.empty();
        WV_LO[t+512]= F.empty()?1:F.front(); WV_HI[t+512]= F.empty()?0:F.back();
        WV_CONTIG[t+512]= F.empty()?1:((i64)F.size()==F.back()-F.front()+1);
    }
}
static inline int wv_score(i64 (*Jf)(int)){ int s=0; for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue;
        i64 j=Jf(t); if(std::binary_search(WV_FEAS[t+512].begin(),WV_FEAS[t+512].end(),j)) s++; } return s; }
