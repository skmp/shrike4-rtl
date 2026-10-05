// rt48_mirror.cpp -- separate the ODD and EVEN parts model-free with the mirror pair (n, 32-n) at the same t:
//     V(n,t) + V(32-n,t) = 2 (C0 - T2)       V(n,t) - V(32-n,t) = 2 (T1 - T3)
// Each side is an interval [lo_n +- lo_m ...].  Count, per word, the offsets where the MODEL's odd part 2(T1-T3) falls
// outside the difference interval (-> the odd model is wrong at that node, independent of the even path) and where the
// model's even part 2(C0-T2) falls outside the sum interval.  Every FSCA violation so far was blamed on the even path;
// this checks that the odd part is clean at the nodes where it cannot be seen alone (it is only exact-checked at 0/32).
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); int totO=0,totE=0,totV=0;
    puts("word | single-window violations (n / 32-n) | ODD part outside the difference interval | EVEN part outside the sum interval | slack of the odd interval: min width");
    for(int n=1;n<=15;n++){ const Tab&T=TB[n]; int vo=0,ve=0,v1=0,v2=0; i64 minw=1<<30;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=sm(arr(T.C1f,dx)+K,23-T.e1); i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=sm(arr(T.c3<<5,A3)+K,26); i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20; i64 T2=sm(arr(J,T.S)+K,T.u2);
            auto win=[&](int m,i64&lo,i64&hi){ int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} };
            i64 l1,h1,l2,h2; win(n,l1,h1); win(32-n,l2,h2); i64 V1=C0TAB[n]+T1-T2-T3, V2=C0TAB[n]-T1-T2+T3; if(V1<l1||V1>h1)v1++; if(V2<l2||V2>h2)v2++;
            i64 od=2*(T1-T3), ev=2*(C0TAB[n]-T2); if(od<l1-h2||od>h1-l2)vo++; if(ev<l1+l2||ev>h1+h2)ve++; minw=std::min(minw,(h1-l2)-(l1-h2)); }
        printf("  %2d |        %3d / %3d                    |            %3d                           |           %3d                      |   %lld\n",n,v1,v2,vo,ve,(long long)minw); totO+=vo; totE+=ve; totV+=v1+v2; }
    printf("total: %d single-window violations; odd part outside %d; even part outside %d\n",totV,totO,totE); return 0; }
