// wa_common.h -- exhaustive two-multiply-chain search for the FSCA square latch J.
// Structure:  I1 = latch1(arr1(const, t<<g1), K1, s1)   [K1 solved EXACTLY over all 2^s1 residues]
//             J  = latch2(arr2(I1,  t<<g2), K2, s2)     [K2 solved EXACTLY as an interval intersection]
// with s1 + s2 = g1 + g2 + 17 (fixes the scale) and e = s1-g1-17 = g2-s2.
#pragma once
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cstring>

// ---------- latch rules ----------
// 0 floor : arithmetic shift of P+K          1 tz  : toward zero of P+K
// 2 sm    : one's-complement magnitude       3 ceil: toward +inf
// 4 nc    : one's complement NOT recomplemented (negative magnitude one too large)
// 5 mag   : K added to the magnitude of P     6 magsm: K added to the one's-complement magnitude
static inline i64 latchv(i64 P, i64 Kc, int s, int rule){
    i64 T = P + Kc;
    switch(rule){
        case 0: return T >> s;
        case 1: return T >= 0 ? (T >> s) : -((-T) >> s);
        case 2: return T >= 0 ? (T >> s) : -((~T) >> s);
        case 3: return -((-T) >> s);
        case 4: return T >= 0 ? (T >> s) : ~((~T) >> s);
        case 5: return P >= 0 ? ((P + Kc) >> s) : -(((-P) + Kc) >> s);
        case 6: return P >= 0 ? ((P + Kc) >> s) : -(((~P) + Kc) >> s);
    }
    return 0;
}
static const char* RULENAME[7] = {"floor","tz","sm","ceil","nc","mag","magsm"};

// ---------- I1 post-latch width handling ----------
// 0 none; 1..8 clear the low m bits; 9..14 keep only the top (18+mm-9) significant bits
static inline i64 applymask(i64 v, int mm){
    if(mm == 0) return v;
    i64 a = v < 0 ? -v : v; i64 sg = v < 0 ? -1 : 1;
    if(mm <= 8){ a &= ~((1ll<<mm)-1); return sg*a; }
    int nbits = 18 + (mm-9);
    if(a == 0) return 0;
    int b = 63 - __builtin_clzll((u64)a);
    int sh = b + 1 - nbits;
    if(sh > 0) a = (a >> sh) << sh;
    return sg*a;
}
static const char* MASKNAME(int mm, char*buf){ if(mm==0) strcpy(buf,"-"); else if(mm<=8) sprintf(buf,"lo%d",mm); else sprintf(buf,"top%d",18+mm-9); return buf; }

// ---------- the J windows ----------
struct Windows { i64 JL[1024], JH[1024]; char ok[1024]; int nfeas; };
static bool load_windows(Windows& W, const char* path="wa_jwin.txt"){
    FILE* f = fopen(path,"r"); if(!f) return false; W.nfeas = 0;
    for(int i=0;i<1024;i++){ int t,o; long long a,b; if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)!=4){fclose(f);return false;}
        W.JL[i]=a; W.JH[i]=b; W.ok[i]=(char)o; W.nfeas+=o; }
    fclose(f); return true; }

// ---------- structure ----------
struct Cfg { i64 cst; int g1,s1,r1,rule1,mm,m1,g2,s2,r2,rule2; };
static inline i64 arrole(i64 a, i64 b, int role){ return role ? arr(b,a) : arr(a,b); }

struct TPre { i64 bp; i64 lo[2], hi[2]; char okiv[2]; i64 v[2]; };

// solve {K2 in [-KR,KR] : jsign*latch2(P2,K2,s2,rule2) in [JL,JH]} as an interval
static inline bool k2_interval(i64 P2,int s2,int rule2,int jsign,i64 JL,i64 JH,i64 KR,i64&olo,i64&ohi){
    auto f=[&](i64 k)->i64{ return (i64)jsign * latchv(P2,k,s2,rule2); };
    i64 fa = f(-KR), fb = f(KR);
    i64 lo,hi;
    if(fa <= fb){
        if(fb < JL || fa > JH) return false;
        i64 a=-KR,b=KR; while(a<b){ i64 m=a+((b-a)>>1); if(f(m)>=JL) b=m; else a=m+1; } lo=a;
        a=-KR; b=KR; while(a<b){ i64 m=a+((b-a+1)>>1); if(f(m)<=JH) a=m; else b=m-1; } hi=a;
    } else {
        if(fa < JL || fb > JH) return false;
        i64 a=-KR,b=KR; while(a<b){ i64 m=a+((b-a+1)>>1); if(f(m)>=JL) a=m; else b=m-1; } hi=a;
        a=-KR; b=KR; while(a<b){ i64 m=a+((b-a)>>1); if(f(m)<=JH) b=m; else a=m+1; } lo=a;
    }
    if(lo>hi) return false; if(f(lo)<JL||f(lo)>JH||f(hi)<JL||f(hi)>JH) return false;
    olo=lo; ohi=hi; return true;
}

static bool prep(const Cfg&C,const Windows&W,const std::vector<int>&TS,std::vector<TPre>&out,i64 KR){
    i64 S1 = 1ll << C.s1; int jsign = C.cst >= 0 ? 1 : -1; out.resize(TS.size());
    for(size_t i=0;i<TS.size();i++){ int t=TS[i]; i64 d1=(i64)t<<C.g1, d2=(i64)t<<C.g2;
        i64 P1 = arrole(C.cst,d1,C.r1);
        i64 vA = latchv(P1,0,C.s1,C.rule1), vB = latchv(P1,S1-1,C.s1,C.rule1);
        i64 bp;
        if(vA==vB) bp = S1;
        else { if(!(vB==vA+1||vB==vA-1)) return false;
               i64 a=0,b=S1-1; while(a<b){ i64 m=a+((b-a)>>1); if(latchv(P1,m,C.s1,C.rule1)!=vA) b=m; else a=m+1; } bp=a; }
        TPre&P=out[i]; P.bp=bp;
        for(int c=0;c<2;c++){
            i64 v = (c==0? vA : vB) + C.m1; v = applymask(v,C.mm); P.v[c]=v;
            i64 P2 = arrole(v,d2,C.r2);
            i64 lo=0,hi=0; P.okiv[c] = k2_interval(P2,C.s2,C.rule2,jsign,W.JL[t+512],W.JH[t+512],KR,lo,hi);
            P.lo[c]=lo; P.hi[c]=hi;
        }
        if(bp==S1){ P.okiv[1]=P.okiv[0]; P.lo[1]=P.lo[0]; P.hi[1]=P.hi[0]; P.v[1]=P.v[0]; }
    }
    return true;
}

// best score over all K1 in [0,S1) and all K2 in [-KR,KR]
static int solve(const Cfg&C,const Windows&W,const std::vector<int>&TS,i64 KR,
                 i64*bestK1=nullptr,i64*bestK2lo=nullptr,i64*bestK2hi=nullptr){
    std::vector<TPre> pre; if(!prep(C,W,TS,pre,KR)) return -1;
    size_t n = TS.size(); i64 S1 = 1ll<<C.s1;
    std::vector<size_t> ord(n); for(size_t i=0;i<n;i++) ord[i]=i;
    std::sort(ord.begin(),ord.end(),[&](size_t a,size_t b){return pre[a].bp<pre[b].bp;});
    std::vector<char> sel(n,0);
    std::vector<std::pair<i64,int>> ev; ev.reserve(2*n);
    int best=0; i64 bk1=0,bk2l=0,bk2h=0;
    for(size_t stage=0;stage<=n;stage++){
        i64 k1;
        if(stage==0) k1=0;
        else { if(pre[ord[stage-1]].bp>=S1) break; sel[ord[stage-1]]=1; k1=pre[ord[stage-1]].bp;
               if(stage<n && pre[ord[stage]].bp==k1) continue; }
        ev.clear();
        for(size_t i=0;i<n;i++){ const TPre&P=pre[i]; int c=sel[i]; if(!P.okiv[c])continue;
            ev.push_back({P.lo[c],1}); ev.push_back({P.hi[c]+1,-1}); }
        std::sort(ev.begin(),ev.end());
        int cur=0; size_t i=0;
        while(i<ev.size()){ i64 x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;}
            if(cur>best){ best=cur; bk1=k1; bk2l=x; bk2h = (i<ev.size()? ev[i].first-1 : x); } }
    }
    if(bestK1)*bestK1=bk1; if(bestK2lo)*bestK2lo=bk2l; if(bestK2hi)*bestK2hi=bk2h;
    return best;
}

// direct evaluation of J for a fully specified structure
static inline i64 Jof(const Cfg&C,i64 K1,i64 K2,int t){
    i64 d1=(i64)t<<C.g1, d2=(i64)t<<C.g2;
    i64 I1 = applymask(latchv(arrole(C.cst,d1,C.r1),K1,C.s1,C.rule1)+C.m1,C.mm);
    i64 J = latchv(arrole(I1,d2,C.r2),K2,C.s2,C.rule2);
    return C.cst>=0? J : -J;
}
static inline int score_exact(const Cfg&C,const Windows&W,i64 K1,i64 K2){
    int sc=0; for(int t=-512;t<=511;t++){ if(!W.ok[t+512])continue; i64 J=Jof(C,K1,K2,t);
        if(J>=W.JL[t+512]&&J<=W.JH[t+512])sc++; } return sc; }
