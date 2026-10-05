// wd_var.cpp -- scan single structural variants of the model; report all four metrics.
#include "wd_core.h"
static const char* Lnames[]={"linK=2^19(base)","linK=2^19+2^(u-1)","linK=2^(u-1)","linK=0","linK=2^19+2^(u-2)"};
int main(){ if(!load_fsca(FF))return 1;
    printf("%-34s  M1(viol) M2(freeJ) M3(infeas t) M4(jwin/1022)\n","variant");
    auto run=[&](const char*name,Cfg c){ Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
        printf("%-34s  %6d  %7d  %9d  %8d\n",name,R.M1,R.M2,R.M3,R.M4); return R; };
    run("baseline",Cfg{});
    for(int i=1;i<=2;i++){ Cfg c; c.flipLin=i; char b[64]; sprintf(b,"flipLin=%d",i); run(b,c); }
    for(int i=1;i<=2;i++){ Cfg c; c.flipCub=i; char b[64]; sprintf(b,"flipCub=%d",i); run(b,c); }
    for(int i=1;i<=4;i++){ Cfg c; c.linK=i; run(Lnames[i],c); }
    for(int i=1;i<=3;i++){ Cfg c; c.cubK=i; char b[64]; sprintf(b,"cubK=%d",i); run(b,c); }
    for(int i=1;i<=3;i++){ Cfg c; c.sqK=i; char b[64]; sprintf(b,"sqK=%d (0/2^(u2-1)/both)",i); run(b,c); }
    for(int i=1;i<=2;i++){ Cfg c; c.a3K=i; char b[64]; sprintf(b,"a3K=%d",i); run(b,c); }
    for(int i=1;i<=4;i++){ Cfg c; c.Smode=i; char b[64]; sprintf(b,"Smode=%d (trunc/24b/half/unclamp)",i); run(b,c); }
    { Cfg c; c.u2off=1; run("u2off=+1",c);} { Cfg c; c.u2off=-1; run("u2off=-1",c); }
    for(int i=1;i<=2;i++){ Cfg c; c.i1K=i; char b[64]; sprintf(b,"i1K=%d",i); run(b,c); }
    for(int i=1;i<=2;i++){ Cfg c; c.i2K=i; char b[64]; sprintf(b,"i2K=%d",i); run(b,c); }
    return 0; }
