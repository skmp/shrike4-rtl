// fv_wiring_8.cpp -- the claim's per-sine-rule table (section 3): coarse free-J floor,
// published final multiply vs the complement-feedback form, for the rules I can state exactly.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                            47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static int kof(int n){ if(n==0)return 0; i128 s=sin_(PI_()*n/32); int k=0; while(s*((i128)1<<(k+1))<QONE)k++; return k; }
static const i64 MM=1ll<<24;
int main(){ if(!load_fsca(F))return 1; build_tables();
    struct Rule{const char*nm;int b;int fixed;};   // b bits, fixed=1 -> no octave normalisation
    Rule rs[8]={{"RN23 normalised u=30+k (published)",23,0},{"RN24 normalised u=31+k",24,0},
                {"RN22 normalised u=29+k",22,0},{"RN21 normalised u=28+k",21,0},
                {"RN20 normalised u=27+k",20,0},{"fixed RN(sin*2^22) u=29",22,1},
                {"fixed RN(sin*2^23) u=30",23,1},{"fixed RN(sin*2^24) u=31",24,1}};
    puts("coarse-latch free-J floor (= 1024 - feasible offsets, each infeasible t costing >=1)");
    puts("  sine rule                              published mult.   feedback form   bad offsets (feedback)");
    for(auto&r:rs){ i64 S[17]; int U[17];
      for(int n=0;n<=16;n++){ int k=kof(n); i128 s=sin_(PI_()*n/32);
        if(!n){S[0]=0;U[0]=30;continue;}
        if(r.fixed){S[n]=rn_scale(s,r.b); U[n]=r.b+7;}
        else {S[n]=std::min(rn_scale(s,r.b+k),(i64)((1ll<<r.b)-1)); U[n]=r.b+7+k;} }
      int fe[2]={0,0}; std::vector<int> bad;
      for(int form=0;form<2;form++)
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000ll;
          i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
          for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
            i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
            i64 t2lo=base-wh,t2hi=base-wl;
            auto g=[&](i64 J)->i64{ return form? ((arr(S[n],MM)-arr(S[n],MM-J)+K)>>U[n]) : sm(arr(J,S[n])+K,U[n]); };
            i64 a1=0,b1=12000001ll; while(a1<b1){i64 m=(a1+b1)/2; if(g(m)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000ll; while(a2<b2){i64 m=(a2+b2+1)/2; if(g(m)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
          if(gl<=gh)fe[form]++; else if(form==1)bad.push_back(t); }
      printf("  %-38s  %6d          %6d        ",r.nm,1024-fe[0],1024-fe[1]);
      if(bad.empty())printf("(none)"); else if(bad.size()<=8){for(int t:bad)printf(" %d",t);} else printf("%zu offsets",bad.size());
      printf("\n"); }
    return 0; }
