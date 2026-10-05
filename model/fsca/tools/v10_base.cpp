// v10_base.cpp -- INDEPENDENT adversarial re-measurement of the FSCA baselines.
// Written from scratch against base2.h + jwin18.txt; no other agent's code reused.
// Build: g++ -O2 -std=c++17 v10_base.cpp -o /tmp/v10_base   (run from tools/)
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdio>

static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

// ---- jwin18.txt loader, parsed by me, with contract assertions -------------------------------
static i64 WL[1024], WH[1024];
static bool load_jwin(const char* path){
    FILE* f=fopen(path,"r"); if(!f){printf("FATAL: cannot open %s\n",path); return false;}
    char line[512]; int seen=0; bool got[1024]={false};
    while(fgets(line,sizeof line,f)){
        if(line[0]=='#'||line[0]=='\n') continue;
        long long t,lo,hi,w;
        if(sscanf(line,"%lld %lld %lld %lld",&t,&lo,&hi,&w)!=4){printf("FATAL: bad line: %s",line);return false;}
        if(t<-512||t>511){printf("FATAL: t out of range %lld\n",t);return false;}
        if(hi-lo+1!=w){printf("FATAL: width column inconsistent at t=%lld\n",t);return false;}
        if(lo>hi){printf("FATAL: infeasible t=%lld\n",t);return false;}
        WL[t+512]=lo; WH[t+512]=hi; got[t+512]=true; seen++;
    }
    fclose(f);
    if(seen!=1024){printf("FATAL: %d lines, expected 1024\n",seen);return false;}
    for(int i=0;i<1024;i++) if(!got[i]){printf("FATAL: missing t=%d\n",i-512);return false;}
    return true;
}
// ---- independently REBUILD the windows from the dump, to check jwin18.txt itself -------------
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 RL[1024], RH[1024];
static void rebuild_jwin(int DU){ const i64 LIM=12000000ll<<DU;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
            // monotone in J (arr(J,S) is nondecreasing for J>=0, S>=0): binary search
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,TB[n].S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,TB[n].S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        RL[t+512]=gl; RH[t+512]=gh; } }

// ---- the baseline J rules ----------------------------------------------------------------------
static inline i64 J_base(int t,int w,int s1,int s2,bool order_dxKC){
    i64 dx=(i64)t<<w;
    i64 I1=((order_dxKC?arr(dx,KC):arr(KC,dx))+K)>>s1;
    return (arr(I1,dx)+K)>>s2;
}
static int score(i64 (*f)(int), int* miss_out=nullptr){ int s=0; for(int t=-512;t<=511;t++){ i64 j=f(t); if(j>=WL[t+512]&&j<=WH[t+512])s++; } if(miss_out)*miss_out=1024-s; return s; }

// ---- full model violation count, with a pluggable square-latch rule ---------------------------
// du = 0 -> coarse latch (J at shift s2c, u2 = 30+k) ; du = 2 -> fine latch (u2 = 32+k)
template<class FJ> static int full_viol(FJ Jf,int du){ int viol=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 J=Jf(t);
            i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]+du):0;
            i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            if(V<lo||V>hi)viol++; } }
    return viol; }

int main(){
    if(!load_fsca(F)){puts("cannot load dump");return 1;}
    build_tables();
    if(!load_jwin("jwin18.txt")) return 1;
    puts("=== v10: independent baseline re-measurement ===");
    // A. does jwin18.txt agree with a window rebuild from the dump?
    rebuild_jwin(2); int diff=0,feas=0; i64 sumw=0,tight=0;
    for(int i=0;i<1024;i++){ if(RL[i]!=WL[i]||RH[i]!=WH[i])diff++; if(RL[i]<=RH[i]){feas++;sumw+=RH[i]-RL[i]+1; if(RH[i]-RL[i]<=3)tight++;} }
    printf("A. jwin18.txt vs my own rebuild from the dump: %d/1024 rows differ; %d feasible, mean width %.1f, %lld pinned<=4\n",
           diff,feas,(double)sumw/feas,(long long)tight);
    printf("   pinned rows: t=17 [%lld,%lld]  t=342 [%lld,%lld]  t=-342 [%lld,%lld]\n",
        (long long)WL[17+512],(long long)WH[17+512],(long long)WL[342+512],(long long)WH[342+512],
        (long long)WL[-342+512],(long long)WH[-342+512]);
    // B. the published square rule, scored on jwin18 at every split of s1+s2=41 (w=13, KC fixed)
    puts("\nB. jwin18 score of J=(arr(I1,dx)+2^19)>>s2, I1=(arr(KC,dx)+2^19)>>s1, s1+s2=41, w=13:");
    int best=-1,bs1=0;
    for(int s1=14;s1<=30;s1++){ int s2=41-s1; int sc=0;
        for(int t=-512;t<=511;t++){ i64 j=J_base(t,13,s1,s2,false); if(j>=WL[t+512]&&j<=WH[t+512])sc++; }
        int scb=0; for(int t=-512;t<=511;t++){ i64 j=J_base(t,13,s1,s2,true); if(j>=WL[t+512]&&j<=WH[t+512])scb++; }
        printf("   (s1,s2)=(%2d,%2d) KC,dx: %4d   dx,KC: %4d%s\n",s1,s2,sc,scb,(s1==23)?"   <== published":"");
        if(sc>best){best=sc;bs1=s1;} }
    printf("   best over the ladder: %d at s1=%d\n",best,bs1);
    // C. pinned landmark values of the published rule
    {   auto P=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
            printf("   t=%+4d: dx=%lld arr(KC,dx)=%lld I1=%lld arr(I1,dx)=%lld J=%lld window[%lld,%lld] %s\n",
                t,(long long)dx,(long long)arr(KC,dx),(long long)I1,(long long)arr(I1,dx),(long long)J,
                (long long)WL[t+512],(long long)WH[t+512],
                (J>=WL[t+512]&&J<=WH[t+512])?"IN":"OUT"); };
        puts("\nC. the three pinned landmarks under the published rule (w=13,s1=23,s2=18,arr(KC,dx)):");
        P(17); P(342); P(-342); }
    // D. full-model violations, both operand orders, coarse and fine latch
    puts("\nD. full model over nodes 0..31 (32768 samples):");
    for(int ro=0;ro<2;ro++){
        auto Jc=[&](int t){ i64 dx=(i64)t<<13; i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K)>>23; return (arr(I1,dx)+K)>>20; };
        auto Jf=[&](int t){ i64 dx=(i64)t<<13; i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K)>>23; return (arr(I1,dx)+K)>>18; };
        printf("   arr(%s): coarse latch (23,20) u2=30+k -> %d violations ; fine latch (23,18) u2=32+k -> %d\n",
            ro?"dx,KC":"KC,dx", full_viol(Jc,0), full_viol(Jf,2)); }
    // E. guard nodes under the published coarse model
    {   auto Jc=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
        printf("\nE. per-node violations (coarse, arr(KC,dx)): ");
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1; int v=0;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 T2=TB[n].S? sm(arr(Jc(t),TB[n].S)+K,U2TAB[n]):0;
                i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
                if(V<lo||V>hi)v++; }
            if(v) printf("n%d:%d ",node,v); }
        puts(""); }
    return 0; }
