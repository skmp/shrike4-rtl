// w5_full.cpp -- branch 5: FULL-MODEL (all 65536 angles) scoring of hypothesis A whole-datapath
// and the node-rule variants of hypothesis B.
#include "w5_model.h"
int main(){ if(!load_fsca(W5F)) return 1; build_tables();
    int vp,vn,mt;
    P5 base; int b=w5_score(base,&vp,&vn,&mt);
    printf("BASELINE rho=512 arr(dx,KC): %d violations over 65536 angles (= 2 x %d)  pos-t %d neg-t %d  max|t| %d\n",
        b,b/2,vp,vn,mt);
    { P5 p=base; p.order=0; printf("  with arr(KC,dx): %d\n",w5_score(p)); }
    printf("\n--- HYPOTHESIS A, whole datapath: t -> t+d for t<0 in linear, square and cubic ---\n");
    for(int d=-4;d<=4;d++){ P5 p=base; p.dlin=p.dsq=p.dcub=d; int a1,a2;
        int s=w5_score(p,&a1,&a2); printf("  d=%+d all three  : %6d violations (pos-t %d, neg-t %d)%s\n",
            d,s,a1,a2,d==1?"   <- 2s computed / 1s read":(d==-1?"   <- converse":"")); }
    printf("\n--- HYPOTHESIS A, per-term placement (d = +1, the 1s-read value) ---\n");
    const char* nm[8]={"none","lin","sq","lin+sq","cub","lin+cub","sq+cub","all"};
    for(int d=-2;d<=2;d++) if(d){ for(int msk=1;msk<8;msk++){ P5 p=base;
        p.dlin=(msk&1)?d:0; p.dsq=(msk&2)?d:0; p.dcub=(msk&4)?d:0;
        printf("  d=%+d %-8s: %6d\n",d,nm[msk],w5_score(p)); } }
    printf("\n--- HYPOTHESIS B, node rule: node = (angle + rho) >> 10 ---\n");
    for(int rho : {0,128,256,384,448,511,512,513,576,640,768,896,1023}){ P5 p=base; p.rho=rho; int a1,a2,m2;
        int s=w5_score(p,&a1,&a2,&m2);
        printf("  rho=%4d : %6d violations   t range half-width %d %s\n",rho,s,m2,rho==512?"  <- current model":""); }
    return 0; }
