// b39.cpp -- publish the corrected latch dataset at the FINE scale (u2 = 32 + k, latch shift 18), where every
// t is feasible and a free latch per t reproduces the whole dump.  Writes jwin18.txt: "t JL JH".
#include "base2.h"
#include <cstring>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables(); const int DU=2; const i64 LIM=12000000ll<<DU;
    FILE*f=fopen("jwin18.txt","w");
    fprintf(f,"# square-path latch window at the FINE scale: T2 = sm(arr(J,S_n) + 2^19, 32 + k_n)\n");
    fprintf(f,"# J ~ 4 * KC * t^2 / 2^17 = 157.88 * t^2 .  columns: t  JL  JH  width\n");
    int tight=0,feas=0; i64 sumw=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;   while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        if(gl<=gh){feas++; sumw+=gh-gl+1; if(gh-gl<=3)tight++;}
        fprintf(f,"%d %lld %lld %lld\n",t,(long long)gl,(long long)gh,(long long)(gh-gl+1)); }
    fclose(f);
    printf("jwin18.txt: %d/1024 t feasible, %d pinned to a window of width <= 4, mean width %.1f\n",
        feas,tight,(double)sumw/feas);
    return 0; }
