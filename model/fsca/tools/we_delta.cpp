// we_delta.cpp -- the exact generator of the chain's sign asymmetry, and what the data demands of it.
// For  I1 = floor((KC*t + E(t))/W)  with E(t) = 48 - g(t mod 8), g = [0,5,2,7,4,1,6,3]:
//     delta(a) = |I1(-a)| - I1(a) = [rho > Em] - [rho >= W - Ep],  rho = (KC*a) mod W
// and  D(a) = J(-a) - J(a) = floor(X + delta*W*a/2^17 + W*(hp-hm)/2^17) - floor(X).
// (1) verify delta predicts D; (2) test the e1-symmetry correlation the brief asks about;
// (3) search (W,E) so that delta = 0 exactly at |t| = 40, 376 and delta = 1 at all 45 forced |t|.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static const int GT[8]={0,5,2,7,4,1,6,3};
static int FORCED[]={86,148,179,189,194,197,201,209,233,249,251,255,258,277,285,299,308,324,325,332,342,344,359,383,385,396,401,408,411,412,415,417,431,444,447,455,456,458,459,469,476,481,487,494,499};
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    // (1)
    int agree=0,tot=0;
    for(int a=1;a<=511;a++){ i64 dp=(i64)a<<13, dm=-dp;
        i64 I1p=(arr(KC,dp)+K)>>23, I1m=(arr(KC,dm)+K)>>23;
        i64 N=(i64)KC*a, rho=N&1023; int Ep=48-GT[a%8], Em=48-GT[(8-a%8)%8];
        int d=( (rho>Em)?1:0 ) - ( (rho>=1024-Ep)?1:0 );
        if(-I1m-I1p==d) agree++; tot++; }
    printf("(1) delta formula matches |I1(-a)|-I1(a) for %d of %d |t|\n",agree,tot);
    // (2) e1 symmetry vs the required D
    int n4=0,n4f=0,nn4=0,nn4f=0;
    for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!JOK[ip]||!JOK[im])continue;
        bool forced=JLo[im]-JHi[ip]>0;
        bool e1sym = (a%4)==0;                       // e1(t)==e1(-t)  <=>  t = 0 mod 4
        if(e1sym){ n4++; if(forced)n4f++; } else { nn4++; if(forced)nn4f++; } }
    printf("(2) e1 symmetric (|t|=0 mod 4): %d |t|, of which %d force D>=1 (%.1f%%)\n",n4,n4f,100.0*n4f/n4);
    printf("    e1 asymmetric           : %d |t|, of which %d force D>=1 (%.1f%%)\n",nn4,nn4f,100.0*nn4f/nn4);
    printf("    -> e1's own symmetry is NOT the generator (both classes force D at a similar rate)\n");
    // delta for the current chain at the special |t|
    for(int a : {40,376,342,324,408,258}){ i64 dp=(i64)a<<13,dm=-dp;
        i64 I1p=(arr(KC,dp)+K)>>23,I1m=(arr(KC,dm)+K)>>23;
        i64 Jp=(arr(I1p,dp)+K)>>20, Jm=(arr(I1m,dm)+K)>>20;
        printf("    |t|=%3d  delta=%lld  D_model=%lld  required D in [%lld,%lld]  (|t| mod 4 = %d)\n",a,
            (long long)(-I1m-I1p),(long long)(Jm-Jp),(long long)(JLo[-a+512]-JHi[a+512]),(long long)(JHi[-a+512]-JLo[a+512]),a%4); }
    // (3) search (W,E): delta==0 at 40 and 376, delta==1 at all 45 forced
    puts("(3) search over (W,E) for a delta that is 0 at |t|=40,376 and 1 at all 45 forced |t|:");
    int bestn=-1; i64 bW=0,bE=0;
    for(int lw=6;lw<=20;lw++){ i64 W=1ll<<lw;
        for(i64 E=0;E<W&&E<40000;E++){ auto del=[&](int a){ i64 rho=((i64)KC*a)%W; return (int)((rho>E?1:0)-(rho>=W-E?1:0)); };
            if(del(40)||del(376)) continue; int n=0; for(int f:FORCED) if(del(f)==1) n++;
            if(n>bestn){bestn=n;bW=W;bE=E;} } }
    printf("    best: W=%lld E=%lld satisfies %d of 45 forced (and delta=0 at 40 and 376)\n",(long long)bW,(long long)bE,bestn);
    // and without the delta=0 requirement
    bestn=-1; for(int lw=6;lw<=20;lw++){ i64 W=1ll<<lw; for(i64 E=0;E<W&&E<40000;E++){
        auto del=[&](int a){ i64 rho=((i64)KC*a)%W; return (int)((rho>E?1:0)-(rho>=W-E?1:0)); };
        int n=0; for(int f:FORCED) if(del(f)==1) n++; if(n>bestn){bestn=n;bW=W;bE=E;} } }
    printf("    without the delta=0 constraint: best %d of 45 (W=%lld,E=%lld)\n",bestn,(long long)bW,(long long)bE);
    return 0; }
