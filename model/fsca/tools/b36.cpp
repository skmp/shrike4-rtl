// b36.cpp -- INDEPENDENT verification of the finer-latch claim: with the square latch taken at shift 18
// instead of 20 (so u2 = 32 + k), is the free-per-t-latch floor really 0, and does t = +-17 close?
// Brute force, no monotonicity assumption: scan the latch directly at every t and evaluate all 31 node instances.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 CLO[32][1024],CHI[32][1024]; static int TABN[32];
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; TABN[node]=n; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CLO[node][t+512]=base-hi; CHI[node][t+512]=base-lo; } }
    printf("shift | u2 rule | free-latch floor (violations over nodes 0..31) | infeasible t | e.g. t=+-17 window\n");
    for(int s2=16;s2<=22;s2++){ int du=20-s2;                    // u2 = 30 + k + du
        i64 tot=0; int infeas=0; i64 w17lo=-1,w17hi=-2;
        #pragma omp parallel for schedule(dynamic) reduction(+:tot,infeas)
        for(int t=-512;t<=511;t++){
            // exact ideal latch value at this shift; scan a generous window around it
            long double ideal=(long double)KC*(long double)t*(long double)t/131072.0L*powl(2.0L,(long double)du);
            i64 c=(i64)ideal, R=400ll<<(du>0?du:0); if(R<400)R=400;
            int bv=1<<30; i64 bj=-1;
            for(i64 J=std::max((i64)0,c-R);J<=c+R;J++){ int v=0;
                for(int node=1;node<=31;node++){ int n=TABN[node]; if(!TB[n].S)continue; int u=TB[n].u2+du;
                    i64 T2=sm(arr(J,TB[n].S)+K,u); i64 V=-T2;   // compare against the shifted constraint
                    if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) v++; if(v>=bv) break; (void)V; }
                if(v<bv){bv=v;bj=J;} if(!bv) break; }
            tot+=bv; if(bv) infeas++;
            if(t==17){ w17lo=bj; w17hi=bv; } }
        printf("  %2d  | 30+k%+d | %6lld                                       | %4d         | best J=%lld with %lld viol\n",
            s2,du,(long long)tot,infeas,(long long)w17lo,(long long)w17hi); fflush(stdout); }
    return 0; }
