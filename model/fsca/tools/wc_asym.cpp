// wc_asym.cpp -- the REQUIRED square-term asymmetry, per (node,|t|), read off the raw windows.
// dT2(node,|t|) = T2(-t) - T2(+t) must lie in [T2LO(-t)-T2HI(+t), T2HI(-t)-T2LO(+t)].
// Print every pair where 0 is excluded (the 34 hard ones) with the sign, and the equivalent dJ.
// C0 shifts a node's whole T2 window uniformly, so dT2's interval is INDEPENDENT of C0.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 T2LO[32][1024], T2HI[32][1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            T2LO[node][t+512]=base-hi; T2HI[node][t+512]=base-lo; } }
    puts("node  |t|   dT2 in [lo,hi]   S        u   dJ_lo    dJ_hi     (dJ = dT2 * 2^u / S)");
    int cnt=0; int pos=0,neg=0;
    for(int node=1;node<32;node++){ int n=node>=17?32-node:node; if(!TB[n].S) continue;
        for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512;
            i64 dlo=T2LO[node][im]-T2HI[node][ip], dhi=T2HI[node][im]-T2LO[node][ip];
            if(dlo<=0&&dhi>=0) continue;
            cnt++; if(dlo>0)pos++; else neg++;
            long double sc=(long double)(1ll<<U2TAB[n])/TB[n].S;
            printf("%4d %4d   [%4lld,%4lld]   %7lld %3d  %8.2Lf %8.2Lf\n",node,a,(long long)dlo,(long long)dhi,
                (long long)TB[n].S,U2TAB[n],dlo*sc,dhi*sc); } }
    printf("total hard pairs %d (dT2>0 i.e. T2(-t)>T2(+t): %d ; dT2<0: %d)\n",cnt,pos,neg);
    // now the union requirement on dJ assuming a shared J: intersect over nodes per |t|
    puts("\n|t|  dJ required interval (intersection over all 31 nodes, integer J)  modelD");
    for(int a=1;a<=511;a++){
        // build per-node dJ interval by inversion of T2 = sm(arr(J,S)+K,u) is what b12 does; here just
        // report the intersection of the SCALED dT2 intervals as a quick estimate
        long double L=-1e18,H=1e18; int any=0;
        for(int node=1;node<32;node++){ int n=node>=17?32-node:node; if(!TB[n].S) continue;
            i64 dlo=T2LO[node][-a+512]-T2HI[node][a+512], dhi=T2HI[node][-a+512]-T2LO[node][a+512];
            long double sc=(long double)(1ll<<U2TAB[n])/TB[n].S;
            long double l=dlo*sc, h=dhi*sc; if(l>L)L=l; if(h<H)H=h; any=1; }
        if(!any) continue;
        i64 dxp=(i64)a<<13, dxm=-dxp;
        i64 I1p=(arr(KC,dxp)+K)>>23, Jp=(arr(I1p,dxp)+K)>>20;
        i64 I1m=(arr(KC,dxm)+K)>>23, Jm=(arr(I1m,dxm)+K)>>20;
        if(L>0.0L||H<0.0L||(H-L)<8.0L)
            printf("%4d  [%8.2Lf, %8.2Lf]    %lld\n",a,L,H,(long long)(Jm-Jp)); }
    return 0; }
