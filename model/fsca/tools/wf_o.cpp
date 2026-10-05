// wf_o.cpp -- (1) which node instances actually pin the latch at the decisive t (how fragile is D(342) in [6,7]?)
//             (2) a PROPER attribution control: free per-t offset in the cubic latch A3 over a range wide
//                 enough to matter (b13 used +-40, but one A3 unit moves T3 by only c3/2^21 ~ 0.0013 of a
//                 grid step, so +-40 could never explain anything).
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
int main(){ if(!load_fsca(F))return 1; build_tables();
  int look[]={59,-59,37,-37,44,-44,63,-63};
  for(int L=0;L<8;L++){ int t=look[L]; i64 dx=(i64)t<<13;
    printf("\n=== t=%d   ideal J = KC t^2/2^17 = %.4f ===\n",t,(double)KC*t*t/131072.0);
    i64 gl=0,gh=12000000; int bl=-1,bh=-1;
    struct R{int node,n,flip;i64 lo,hi;bool ex;};
    std::vector<R> rs;
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
      i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
      i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
      i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
      rs.push_back({node,n,flip,a1,a2,ex}); if(a1>gl){gl=a1;bl=node;} if(a2<gh){gh=a2;bh=node;} }
    printf("intersection [%lld, %lld]   lower bound set by node %d, upper by node %d\n",(long long)gl,(long long)gh,bl,bh);
    // how many nodes are within 3 of the binding ends?
    for(auto&r:rs) if(r.lo>=gl-40||r.hi<=gh+40)
      printf("   node %2d (tab %2d%s) [%8lld,%8lld]%s%s%s\n",r.node,r.n,r.flip?" flip":"     ",(long long)r.lo,(long long)r.hi,
        r.ex?"  Vexact":"", r.lo==gl?"   <-LOW":"", r.hi==gh?"   <-HIGH":""); }
  // (2) attribution control with a proper range
  printf("\n=== attribution control (free per-t offset, range as stated) ===\n");
  for(int mode=0;mode<3;mode++){
    int R = mode==0? 60 : (mode==1? 6000 : 6000);
    long long tot=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
      i64 I1=(arr(KC,dx)+K)>>23, I2=(arr(I1,dx)+K)>>20;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
      struct N{ i64 C0,S,c3; int u; i64 lo,hi; int s; };
      std::vector<N> nl;
      for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        nl.push_back({C0TAB[n]+s*T1,T.S,T.c3,U2TAB[n],lo,hi,(int)s}); }
      // node 0 is exact with no square/cubic term, skip
      int bv=1<<30;
      for(int d=-R;d<=R;d++){ int v=0;
        for(auto&n:nl){ i64 T2,T3;
          if(mode==0){ T2=n.S? sm(arr(I2+d,n.S)+K,n.u):0; T3=n.c3? sm(arr(n.c3<<5,A3)+K,26):0; }
          else if(mode==1){ T2=n.S? sm(arr(I2,n.S)+K,n.u):0; T3=n.c3? sm(arr(n.c3<<5,A3+d)+K,26):0; }
          else { i64 A3b=((arr((((i64)t*t)+d)<<3,dx)+K)>>20)>>3; T2=n.S? sm(arr(I2,n.S)+K,n.u):0; T3=n.c3? sm(arr(n.c3<<5,A3b)+K,26):0; }
          i64 V=n.C0-T2-n.s*T3; if(V<n.lo||V>n.hi)v++; }
        if(v<bv)bv=v; }
      tot+=bv; }
    const char* mn[3]={"square latch I2, +-60","cubic latch A3, +-6000","t^2 latch feeding the cubic, +-6000"};
    printf("free per-t offset in %-38s : %lld window violations over nodes 1..31\n",mn[mode],tot); }
  return 0; }
