// wd_base.cpp -- reproduce the baseline with the parameterised harness, and verify T2(J) monotonicity.
#include "wd_core.h"
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    Model M; build_model(M);
    printf("derived S/u2:\n"); for(int n=0;n<=16;n++) printf("  n=%2d S=%8lld u2=%d\n",n,(long long)M.S[n],M.u2[n]);
    int nc[17]; fit_C0(M,nc);
    printf("fitted C0 (candidates in t=0 window):\n");
    for(int n=0;n<=16;n++) printf("  n=%2d C0=%9lld  cand=%d  (RN26 delta %+lld)\n",n,(long long)M.C0[n],nc[n],(long long)(M.C0[n]-TB[n].sin26));
    if(argc>1&&!strcmp(argv[1],"mono")){
        int bad=0;
        for(int n=1;n<=16;n++){ i64 prev=-1; for(i64 J=0;J<=JMAX;J++){ i64 v=sq_term(J,M.S[n],M.u2[n],M.c); if(v<prev){bad++; if(bad<5)printf("NONMONO n=%d J=%lld %lld->%lld\n",n,(long long)J,(long long)prev,(long long)v);} prev=v; } }
        printf("monotonicity violations: %d\n",bad); }
    Metrics R=evaluate(M,true);
    printf("M1(chain viol)=%d  M2(free-J bound)=%d  M3(infeasible t)=%d  M4(jwin score)=%d\n",R.M1,R.M2,R.M3,R.M4);
    printf("per-node:"); for(int i=0;i<32;i++) if(R.nodeviol[i])printf(" %d:%d",i,R.nodeviol[i]); printf("\n");
    return 0; }
