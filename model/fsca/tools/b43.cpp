// b43.cpp -- verify the agents' surviving structural claims, and check the two that were refuted.
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
    // (1) the closed form for the first array's truncation
    int bad=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 e1=arr(KC,dx)-(i64)KC*dx;
        int m=((t%8)+8)%8; int g=m^(4*(m&1)); i64 pred=-(1ll<<17)-(1ll<<13)*(i64)g; if(e1!=pred)bad++; }
    printf("(1) e1(t) = arr(KC,dx) - KC*dx == -2^17 - 2^13 * (m XOR 4(m&1)), m = t mod 8 : %d mismatches of 1024\n",bad);
    // (2) mirror sum cancels the odd part exactly
    bad=0; for(int n=1;n<=15;n++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
        i64 T2=TB[n].S? sm(arr(((arr(KC,dx)+K)>>23),dx)+K,U2TAB[n]):0;   // any T2; it cancels out of the test
        i64 Va=C0TAB[n]+T1-T2-T3, Vb=C0TAB[n]-T1-T2+T3;
        if(Va+Vb != 2*(C0TAB[n]-T2)) bad++; }
    printf("(2) V(node n) + V(node 32-n) == 2*(C0 - T2) exactly (T1, T3 cancel): %d mismatches\n",bad);
    // (3) C0 uniqueness: for every table entry, every candidate in the t=0 window, the free-latch feasibility at shift 18
    puts("(3) C0 candidates at the fine latch scale (free-latch infeasible-t count per candidate):");
    for(int n=1;n<=16;n++){ i64 l,h;bool ex; win_of(n*1024,l,h,ex); if(h==l) continue;
        printf("    n=%2d :",n);
        for(i64 c=l;c<=h;c++){ int infeas=0;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=48000000;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
                for(int node=1;node<=31;node++){ int flip=node>=17,nn=flip?32-node:node; if(!TB[nn].S)continue; i64 s=flip?-1:1;
                    i64 C0 = (nn==n)? c : C0TAB[nn];
                    i64 T1=TB[nn].C1f? sm(arr(TB[nn].C1f,dx)+K,23-TB[nn].e1):0;
                    i64 T3=TB[nn].c3? sm(arr(TB[nn].c3<<5,A3)+K,26):0;
                    i64 base=C0+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool e2; win_of(a,lo,hi,e2);
                    i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[nn]+2;
                    i64 a1=0,b1=48000001; while(a1<b1){i64 mm=(a1+b1)/2; if(sm(arr(mm,TB[nn].S)+K,u)>=t2lo)b1=mm;else a1=mm+1;}
                    i64 a2=-1,b2=48000000; while(a2<b2){i64 mm=(a2+b2+1)/2; if(sm(arr(mm,TB[nn].S)+K,u)<=t2hi)a2=mm;else b2=mm-1;}
                    if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
                if(gl>gh) infeas++; }
            printf("  %lld -> %d",(long long)c,infeas); }
        printf("\n"); }
    // (4) does the s2=18 CHAIN actually help?  match the rounding constants fairly
    puts("(4) chain rule at each latch shift, with the second rounding constant 2^19 and 0:");
    for(int s2=17;s2<=21;s2++){ int du=20-s2;
        for(int k2i=0;k2i<2;k2i++){ i64 K2=k2i?0:(1ll<<19); int viol=0;
            for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
                for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                    i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                    i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K2)>>s2;
                    i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,U2TAB[n]+du):0;
                    i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    if(V<lo||V>hi)viol++; } }
            printf("    s2=%2d u2=30+k+%d K2=%-7lld -> %d window violations\n",s2,du,(long long)K2,viol); } }
    return 0; }
