// we_sq.cpp -- is the array's own dx*dx truncation SIGN-ASYMMETRIC?
// esq(t) = arr(dx,dx) - dx*dx.  If esq(t) != esq(-t) for some t, a dx^2-first square path is NOT
// symmetric after all, and the asymmetry it carries is (esq(-t)-esq(t)) * C / 2^(s+s2).
#include "we_win.h"
#include <cstring>
#include <algorithm>
#include <map>
int main(){ if(!load_fsca(WF))return 1; build_tables();
    int diff=0; std::map<i64,int> dv;
    printf("# t  esq(+t)   esq(-t)   diff\n");
    for(int a=1;a<=511;a++){ i64 dp=(i64)a<<13, dm=-dp;
        i64 ep=arr(dp,dp)-dp*dp, em=arr(dm,dm)-dm*dm;
        if(ep!=em){ diff++; dv[em-ep]++; if(diff<=25) printf("%4d %9lld %9lld %+9lld\n",a,(long long)ep,(long long)em,(long long)(em-ep)); } }
    printf("|t| with esq(+)!=esq(-): %d of 511 ; differences:",diff);
    for(auto&p:dv) printf(" %+lld x%d",(long long)p.first,p.second); puts("");
    // which |t| are they, and do they line up with the 45 forced-D values?
    std::vector<int> asym;
    for(int a=1;a<=511;a++){ i64 dp=(i64)a<<13,dm=-dp; if(arr(dp,dp)-dp*dp != arr(dm,dm)-dm*dm) asym.push_back(a); }
    printf("asymmetric |t| :"); for(int a:asym) printf(" %d",a); puts("");
    int forced[]={86,148,179,189,194,197,201,209,233,249,251,255,258,277,285,299,308,324,325,332,342,344,359,383,385,396,401,408,411,412,415,417,431,444,447,455,456,458,459,469,476,481,487,494,499};
    int hit=0; for(int f:forced) if(std::find(asym.begin(),asym.end(),f)!=asym.end()) hit++;
    printf("forced-D |t| that are esq-asymmetric: %d of 45\n",hit);
    // also: esq as a function of what?
    printf("esq(+t) by t mod 8: "); for(int m=0;m<8;m++){ std::map<i64,int> s; for(int a=1;a<=511;a++) if(a%8==m){ i64 d=(i64)a<<13; s[arr(d,d)-d*d]++; }
        printf("m=%d{",m); for(auto&p:s)printf("%lld:%d ",(long long)p.first,p.second); printf("} "); } puts("");
    return 0; }
