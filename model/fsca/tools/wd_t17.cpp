// wd_t17.cpp -- focused scan of the two tables binding at t=+-17.  Reports, for each perturbed S,
// whether t=+-17 becomes feasible and what it costs over the whole dump.
#include "wd_core.h"
static bool feas_t(Model&M,int t,i64*gl=nullptr,i64*gh=nullptr){
    i64 a=0,b=JMAX;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
        int ang=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(ang,l,h,e);
        i64 bs=base_of(M,n,t,flip); i64 aa,bb; if(!jint(M,n,bs-h,bs-l,aa,bb)) return false;
        if(aa>a)a=aa; if(bb<b)b=bb; }
    if(gl)*gl=a; if(gh)*gh=b; return a<=b; }
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int tab=argc>1?atoi(argv[1]):8; int lo=argc>2?atoi(argv[2]):-20, hi=argc>3?atoi(argv[3]):120;
    Model B; build_model(B); fit_C0(B); Metrics R0=evaluate(B);
    printf("baseline M1=%d M2=%d M3=%d M4=%d ; S_%d=%lld u2=%d\n",R0.M1,R0.M2,R0.M3,R0.M4,tab,(long long)B.S[tab],B.u2[tab]);
    printf(" dS      S      t17feas  Jwin@17      M1   M2  M3   M4\n");
    for(int dS=lo;dS<=hi;dS++){ Cfg c; c.Sov[tab]=B.S[tab]+dS; Model M; M.c=c; build_model(M); fit_C0(M);
        Metrics R=evaluate(M); build_thr(M); i64 gl=0,gh=0; bool f=feas_t(M,17,&gl,&gh);
        if(dS%1==0) printf("%+4d %9lld   %s  [%6lld,%6lld] %5d %4d %3d %5d%s\n",dS,(long long)(B.S[tab]+dS),
            f?"YES":"no ",(long long)gl,(long long)gh,R.M1,R.M2,R.M3,R.M4, (R.M3==0)?"  <== ALL FEASIBLE":""); }
    return 0; }
