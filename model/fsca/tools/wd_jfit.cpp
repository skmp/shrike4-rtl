// wd_jfit.cpp -- what does the data require of the square latch J(t)?  Print the allowed window
// against the ideal 4*pi^2*t^2 and against KC*t^2/2^17, in exact integer arithmetic where possible.
#include "wd_core.h"
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    Model M; build_model(M); fit_C0(M); build_thr(M);
    int lim = argc>1?atoi(argv[1]):40;
    printf("  t      JL       JH   width   chainJ   KC*t^2/2^17   4pi^2 t^2 (x1000)   JL-ideal  JH-ideal\n");
    long double fp = 4.0L*acosl(-1.0L)*acosl(-1.0L);
    for(int t=-lim;t<=lim;t++){
        i64 gl=0,gh=JMAX; bool feas=true;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 b=base_of(M,n,t,flip); i64 aa,bb; if(!jint(M,n,b-h,b-l,aa,bb)){feas=false;continue;}
            if(aa>gl)gl=aa; if(bb<gh)gh=bb; }
        i64 kc = (i64)KC*t*t/131072; long double id = fp*(long double)t*t;
        printf("%4d %8lld %8lld %6lld %8lld %9lld %18.3Lf %9.3Lf %9.3Lf%s\n",t,(long long)gl,(long long)gh,
            (long long)(gh-gl+1),(long long)chainJ(t,M.c),(long long)kc,id*1000,(long double)gl-id,(long double)gh-id,
            (!feas||gl>gh)?"  INFEASIBLE":""); }
    return 0; }
