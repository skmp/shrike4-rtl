// c6_k.cpp -- cross-instruction check.  The FSRRA notes independently arrive at a chain-intermediate rounding
// constant of 2^19 + 2^16 (K(I) = 2^19 or 2^19+2^16, 0.0154% misses).  Does FSCA's square chain want the same?
// Scan K1 over 2^19 + j*2^16 and over all small multiples of 2^16, scoring the full model and the guard nodes.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts("K1 (first square latch)      total   guard(n=0,1,2,16)   note");
    for(int j=-4;j<=12;j++){ i64 K1=(1ll<<19)+(i64)j*(1ll<<16); if(K1<0) continue;
        for(int ro=0;ro<2;ro++){
            int viol=0,guard=0;
            for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
                for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                    i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                    i64 I1=((ro? arr(dx,KC):arr(KC,dx))+K1)>>23, J=(arr(I1,dx)+K)>>20;
                    i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]):0;
                    i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    if(V<lo||V>hi){ viol++; if(n==0||n==1||n==2||n==16) guard++; } } }
            printf("2^19 %+3d*2^16 = %-8lld %s  %4d      %3d           %s\n",j,(long long)K1,
                ro?"arr(dx,KC)":"arr(KC,dx)",viol,guard, (j==0?"<- the assumed value":(j==1?"<- what FSRRA prefers":"")));
        } }
    return 0; }
