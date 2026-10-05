// wv_orders_floor.cpp -- independent free-J floor (interval stabbing) + monotonicity audit of T2(J).
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
int main(){ if(!load_fsca(F))return 1; build_tables();
    // A. monotonicity of T2(J)=sm(arr(J,S)+K,u) in J, sampled densely over the whole J range
    puts("== monotonicity of T2(J) (non-decreasing?) over J in [0,11000000], step 1, per table ==");
    int bad=0;
    #pragma omp parallel for reduction(+:bad) schedule(dynamic)
    for(int n=1;n<=16;n++){ i64 prev=sm(arr(0,TB[n].S)+K,U2T[n]); int b=0;
        for(i64 j=1;j<=11000000;j++){ i64 v=sm(arr(j,TB[n].S)+K,U2T[n]); if(v<prev)b++; prev=v; }
        if(b) printf("  table %2d: %d decreases\n",n,b); bad+=b; }
    printf("  total monotonicity violations: %d\n",bad);
    // B. free per-t J: interval stabbing over the 31 node instances with S != 0
    int tot=0; std::vector<int> badt;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        std::vector<std::pair<i64,i64>> iv;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; if(!T.S)continue;
            i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0T[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2T[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            iv.push_back({a1,a2}); }
        // max number of simultaneously satisfiable intervals = max stabbing depth
        std::vector<std::pair<i64,int>> ev;
        for(auto&p:iv){ if(p.first>p.second) continue; ev.push_back({p.first,+1}); ev.push_back({p.second+1,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(auto&e:ev){cur+=e.second; if(cur>mx)mx=cur;}
        int empt=0; for(auto&p:iv) if(p.first>p.second) empt++;
        int miss=(int)iv.size()-mx; tot+=miss; if(miss){ badt.push_back(t); printf("  t=%5d : %d of %d node instances unsatisfiable (empty intervals %d)\n",t,miss,(int)iv.size(),empt); } }
    printf("== free per-t J floor over nodes 1..31 (node 0 has S=0, always exact): %d violations ==\n",tot);
    return 0; }
