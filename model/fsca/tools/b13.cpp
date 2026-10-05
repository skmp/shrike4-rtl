// b13.cpp -- control: which shared latch is actually wrong?  Minimum violations with a free per-t offset in
//   (a) the square latch I2 only, (b) the cubic latch A3 only, (c) both, (d) the t^2 latch feeding the cubic.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    const int R=40;
    int tot_i2=0,tot_a3=0,tot_both=0;
    int worst_i2=0,worst_a3=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23, I2=(arr(I1,dx)+K)>>20;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        struct N{ i64 C0,T1,S; i64 c3; int u; i64 lo,hi; };
        std::vector<N> nl;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            nl.push_back({C0TAB[n]+s*T1, s, T.S, s*T.c3, U2TAB[n], lo, hi}); }
        auto cnt=[&](int di2,int da3){ int v=0; for(auto&n:nl){
            i64 T2=n.S? sm(arr(I2+di2,n.S)+K,n.u):0;
            i64 T3=0; if(n.c3){ i64 cc=n.c3<0?-n.c3:n.c3; i64 tt=sm(arr(cc<<5,A3+da3)+K,26); T3=n.c3<0?-tt:tt; }
            i64 V=n.C0-T2-T3; if(V<n.lo||V>n.hi)v++; } return v; };
        int b1=1<<30,b2=1<<30,b3=1<<30;
        for(int d=-R;d<=R;d++){ b1=std::min(b1,cnt(d,0)); b2=std::min(b2,cnt(0,d)); }
        for(int d=-R;d<=R;d++) for(int e=-8;e<=8;e++) b3=std::min(b3,cnt(d,e));
        tot_i2+=b1; tot_a3+=b2; tot_both+=b3; worst_i2=std::max(worst_i2,b1); worst_a3=std::max(worst_a3,b2); }
    printf("min violations, free per-t offset in the SQUARE latch I2 only : %d (worst single t: %d)\n",tot_i2,worst_i2);
    printf("min violations, free per-t offset in the CUBIC  latch A3 only : %d (worst single t: %d)\n",tot_a3,worst_a3);
    printf("min violations, free offsets in BOTH                          : %d\n",tot_both);
    return 0; }
