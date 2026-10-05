// wd_pert.cpp -- per-table perturbation of the square-path table entry (S, u2) and of C0.
// Reports M1 (chain viol), M2 (free-J bound), M3 (infeasible t).  Only changes that keep
// nodes 0,1,2,16 exact AND do not raise M2 are interesting.
#include "wd_core.h"
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int only = argc>1?atoi(argv[1]):-1;
    Model B; build_model(B); fit_C0(B); Metrics R0=evaluate(B);
    printf("baseline M1=%d M2=%d M3=%d M4=%d\n",R0.M1,R0.M2,R0.M3,R0.M4);
    printf("tab  dS  du2 |  M1   M2  M3  M4   (S,u2)\n");
    for(int n=1;n<=16;n++){ if(only>=0&&n!=only)continue;
        for(int du=-1;du<=1;du++) for(int dS=-40;dS<=40;dS++){
            Cfg c; int kdum; i64 S0=Sval(n,0,kdum);
            i64 S = (du==0? S0 : du>0? S0/2 : S0*2) + dS;
            if(S<=0||S>=(1ll<<24)) continue;
            c.Sov[n]=S; c.u2ov[n]=30+kdum+du;
            Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
            if(R.M2<=R0.M2 && R.M3<=R0.M3 && (R.M2<R0.M2||R.M3<R0.M3||R.M1<R0.M1))
                printf("%3d %+4d %+3d |%5d %4d %3d %5d   (%lld,%d)%s\n",n,dS,du,R.M1,R.M2,R.M3,R.M4,
                    (long long)S,30+kdum+du, (R.M3<R0.M3)?"   <== FEASIBILITY GAIN":"");
        } }
    return 0; }
