// rt49_events.cpp -- the even-part events of rt48 (model-free sum interval of the mirror pair), listed: word, t, model T2,
// admissible T2 interval, the raw distance of the model's product arr(J,S)+K to the boundary it would have to cross,
// expressed (a) raw, (b) in J units (raw / S), (c) in output units (raw / 2^u2).
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); puts("word k     t        J   model T2   T2 must be in      dir |  raw gap     J units   out units |  J mod 8  t mod 8");
    for(int n=1;n<=16;n++){ const Tab&T=TB[n];
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20; i64 P=arr(J,T.S)+K; i64 T2=sm(P,T.u2);
            auto win=[&](int m,i64&lo,i64&hi){ int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} };
            i64 l1,h1,l2,h2; win(n,l1,h1); if(n<16)win(32-n,l2,h2); else { l2=l1; h2=h1; }
            // 2(C0 - T2) in [l1+l2, h1+h2]  ->  T2 in [C0 - (h1+h2)/2, C0 - (l1+l2)/2]
            i64 tlo=C0TAB[n]-((h1+h2)>>1), thi=C0TAB[n]-((l1+l2+1)>>1); if(T2>=tlo&&T2<=thi)continue; int dir=T2>thi?-1:+1; i64 fr=P&((1ll<<T.u2)-1); i64 gap=dir<0?fr+1:(1ll<<T.u2)-fr;
            printf("  %2d %d  %5d %9lld  %8lld   [%8lld,%8lld]  %+d | %9lld  %8.2f   %8.4f  |    %lld       %d\n",n,T.u2-30,t,(long long)J,(long long)T2,(long long)tlo,(long long)thi,dir,(long long)gap,(double)gap/(double)T.S,(double)gap/(double)(1ll<<T.u2),(long long)(J&7),t&7); } }
    return 0; }
