// wf_u.cpp -- end-to-end evaluator.  Runs the full structural model over nodes 0..31 with a pluggable rule
// for the square latch J, and reports window violations per node (baseline 148, theoretical floor 2), the
// score against the latch windows, and whether the guard nodes 0,1,2,16 stay exact.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static int OKW[1024];
// --- the rules -------------------------------------------------------------
static i64 Q24=0,B24=0,C24=0;
static i64 Jrule(int r,int t){ i64 dx=(i64)t<<13;
  switch(r){
    case 0: { i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }                      // current model
    case 1: { i64 I1=(arr(dx,20694294)+K)>>25; return (arr(I1,dx))>>20; }                  // wf_f best chain
    case 2: { return (Q24*(i64)t*t + B24*t + C24)>>24; }                                   // fitted affine-in-t^2 floor
    case 3: { i64 I1=(arr(dx,KC)+K)>>23; return (arr(I1,dx)+K)>>20; }
  } return 0; }
int main(){ if(!load_fsca(F))return 1; build_tables();
  { FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKW[t+512]=o;} fclose(f); }
  // fit rule 2 exactly in integers at the 2^24 scale
  { i64 base=(i64)KC<<7; int best=-1;
    #pragma omp parallel
    { int lb=-1; i64 lq=0,lbb=0,lc=0;
      #pragma omp for schedule(dynamic)
      for(int qi=-600;qi<=600;qi++) for(int bi=-120000;bi<=120000;bi+=200){ i64 Q=base+qi, B=bi;
        std::vector<std::pair<i64,int>> ev; ev.reserve(2044);
        for(int t=-512;t<=511;t++){ if(!OKW[t+512])continue; i64 v=Q*(i64)t*t+B*t;
          ev.push_back({(JL[t+512]<<24)-v,1}); ev.push_back({((JH[t+512]+1)<<24)-v,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,mx=0; i64 bc=0;
        for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>mx){mx=cur;bc=ev[i].first;}}
        if(mx>lb){lb=mx;lq=Q;lbb=B;lc=bc;} }
      #pragma omp critical
      { if(lb>best){best=lb;Q24=lq;B24=lbb;C24=lc;} } }
    printf("rule 2 fit: J = (Q*t^2 + B*t + C) >> 24  with Q=%lld (KC<<7 = %lld, delta %+lld), B=%lld, C=%lld -> latch score %d\n",
      (long long)Q24,(long long)((i64)KC<<7),(long long)(Q24-((i64)KC<<7)),(long long)B24,(long long)C24,best); }
  for(int r=0;r<4;r++){
    int lscore=0; for(int t=-512;t<=511;t++){ if(!OKW[t+512])continue; i64 J=Jrule(r,t); if(J>=JL[t+512]&&J<=JH[t+512])lscore++; }
    int grand=0; int per[32]; memset(per,0,sizeof per); int guard=1;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 J=Jrule(r,t); i64 T2=T.S? sm(arr(J,T.S)+K,U2TAB[n]):0;
        i64 V=C0TAB[n]+s*T1-T2-s*T3;
        int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        if(V<lo||V>hi){ per[node]++; grand++; } } }
    if(per[0]||per[1]||per[2]||per[16]) guard=0;
    printf("\nrule %d: latch score %d/1022, window violations over nodes 0..31 = %d, guard nodes 0/1/2/16 = %d/%d/%d/%d%s\n",
      r,lscore,grand,per[0],per[1],per[2],per[16],guard?"  (guards exact)":"  (GUARD BROKEN)");
    printf("   per node:"); for(int i=0;i<32;i++) printf(" %d",per[i]); printf("\n"); }
  return 0; }
