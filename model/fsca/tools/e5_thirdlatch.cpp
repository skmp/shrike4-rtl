// e5_thirdlatch.cpp -- a THIRD truncation in the square path, between the chain and the final multiply.
// Flagged as untested by the wc_ investigation and never run.  It matters because a re-truncation with its OWN
// rounding constant is NOT the same as computing the chain at a coarser shift:
//     (((arr+K2)>>s2) + K3) >> g     vs     (arr + K2') >> (s2+g)
// the double truncation loses differently.  That gives the square path a granularity independent of s2, which
// is exactly the degree of freedom the fixed-alignment chain lacks (there asymmetry and resolution are one
// quantity).  Scale law: with Jf = (J+K3)>>g the final shift must take du = 43 - s1 - s2 - g.
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
    int best=1<<30, bg=0,bs1=0,bs2=0,bk1=0,bk2=0; i64 bk3=0; int bro=0;
    int bestguard=1<<30; int gg=0,gs1=0,gs2=0,gk1=0,gk2=0; i64 gk3=0; int gro=0;
    printf("scanning: first multiply role, s1, s2, extra shift g, three rounding constants\n");
    for(int ro=0;ro<2;ro++)
    for(int s1=18;s1<=28;s1++) for(int s2=10;s2<=28;s2++) for(int g=0;g<=5;g++){
      int du=43-s1-s2-g; 
      for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3i=0;k3i<3;k3i++){
        i64 K1=k1?(1ll<<19):0, K2=k2?(1ll<<19):0;
        i64 K3 = k3i==0?0 : (k3i==1? (g? (1ll<<(g-1)):0) : (1ll<<19));
        int viol=0,guard=0; bool bad=false;
        for(int node=0;node<=31&&!bad;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
            int u=U2TAB[n]+du; if(u<0||u>60){ bad=true; break; }
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 I1=((ro? arr(dx,KC):arr(KC,dx))+K1)>>s1;
                i64 J=(arr(I1,dx)+K2)>>s2;
                i64 Jf=(J+K3)>>g;
                i64 T2=TB[n].S? sm(arr(Jf,TB[n].S)+K,u):0;
                i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                if(V<lo||V>hi){ viol++; if(n==0||n==1||n==2||n==16) guard++; }
                if(viol>400){ bad=true; break; } } }
        if(bad) continue;
        if(viol<best){ best=viol; bg=g; bs1=s1; bs2=s2; bk1=k1; bk2=k2; bk3=K3; bro=ro; }
        if(guard==0&&viol<bestguard){ bestguard=viol; gg=g; gs1=s1; gs2=s2; gk1=k1; gk2=k2; gk3=K3; gro=ro; }
      } }
    printf("\nbest overall      : %d violations  (g=%d s1=%d s2=%d K1=%s K2=%s K3=%lld first=%s)\n",
        best,bg,bs1,bs2,bk1?"2^19":"0",bk2?"2^19":"0",(long long)bk3,bro?"arr(dx,KC)":"arr(KC,dx)");
    printf("best guard-clean  : %d violations  (g=%d s1=%d s2=%d K1=%s K2=%s K3=%lld first=%s)\n",
        bestguard,gg,gs1,gs2,gk1?"2^19":"0",gk2?"2^19":"0",(long long)gk3,gro?"arr(dx,KC)":"arr(KC,dx)");
    printf("baseline for comparison: 147 guard-clean, 140 with the guard broken\n");
    return 0; }
