// rt38_c0.cpp -- is C0 a ROM constant, or is it ABSORBING a modelling error?
// In our model every product vanishes at t = 0, so C0 := V(0).  But for nodes >= 3 the t = 0 window is 2..3 wide, and C0
// was "pinned by feasibility" UNDER OUR EVEN PART.  Compare, per node: the hardware's own t = 0 window, the clean values a
// ROM generator would produce (RN of sin, RN of the Chebyshev-economised constant sin (1 - h^4/192)), and the fitted C0.
#include "base2.h"
#include <cmath>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); long double PI=acosl(-1.0L), h=512*2*PI/65536, econ=powl(h,4)/192;
    printf("Chebyshev constant term: sin * (1 - %.4Le)  = sin*2^26 - %.3Lf*sin units\n\n",econ,econ*67108864.0L);
    puts("node  hw window at t=0 (nodes n and 32-n)      sin*2^26        RN(sin)   RN(econ)   fitted C0   fitted-RN(sin)  fitted-RN(econ)   clean value inside hw window?   violations(word)");
    int viol[17]={0,0,0,6,4,16,13,8,4,22,14,12,11,17,11,10,0};
    for(int n=1;n<=16;n++){ long double x=sinl(PI*n/32)*67108864.0L, xe=x*(1-econ); i64 rn=(i64)llroundl(x), re=(i64)llroundl(xe);
        Win w=decode(F.srz[n*1024],F.srn[n*1024]); Win w2=decode(F.srz[(32-n)*1024],F.srn[(32-n)*1024]); i64 lo=std::max(w.lo,w2.lo), hi=std::min(w.hi,w2.hi);
        printf(" %2d   [%lld,%lld]%s   %14.3Lf  %9lld  %9lld  %9lld      %+3lld            %+3lld            RN(sin):%s RN(econ):%s fitted:%s          %2d\n",n,(long long)lo,(long long)hi,(w.lo==w2.lo&&w.hi==w2.hi)?"":"*",x,(long long)rn,(long long)re,(long long)C0TAB[n],(long long)(C0TAB[n]-rn),(long long)(C0TAB[n]-re),
               (rn>=lo&&rn<=hi)?"yes":"NO ",(re>=lo&&re<=hi)?"yes":"NO ",(C0TAB[n]>=lo&&C0TAB[n]<=hi)?"yes":"NO ",viol[n]); }
    return 0; }
