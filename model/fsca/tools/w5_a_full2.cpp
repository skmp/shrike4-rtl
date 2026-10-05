// w5_a_full2.cpp -- BRANCH 5: the 1s-vs-2s confusion at the dx LEVEL (offset +-q on the negative
// branch) and on the I1 feedback, scored on the FULL model over all 65536 angles.  The earlier m*
// scan only ever scored the square path against jwin18, so the whole-datapath version is new.
#include "w5_model.h"
#include <cstdio>
int main(){ if(!load_fsca(W5F))return 1; build_tables();
    P5 base; printf("BASELINE: %d violations (147 x 2)\n\n",w5_score(base));
    printf("--- dx-level offset q on t<0, applied to ALL THREE terms ---\n");
    for(int q=-4;q<=4;q++){ P5 p=base; p.qlin=p.qsq=p.qcub=q; int a1,a2;
        int s=w5_score(p,&a1,&a2); printf("  q=%+d : %6d  (pos-t %d, neg-t %d)%s\n",q,s,a1,a2,q?"":"  <- baseline"); }
    printf("\n--- dx-level offset q on t<0, square path only ---\n");
    for(int q=-4;q<=4;q++){ P5 p=base; p.qsq=q; int a1,a2;
        int s=w5_score(p,&a1,&a2); printf("  q=%+d : %6d  (pos-t %d, neg-t %d)\n",q,s,a1,a2); }
    printf("\n--- I1 feedback offset r on t<0 ---\n");
    for(int r=-4;r<=4;r++){ P5 p=base; p.rfb=r; int a1,a2;
        int s=w5_score(p,&a1,&a2); printf("  r=%+d : %6d  (pos-t %d, neg-t %d)\n",r,s,a1,a2); }
    printf("\n--- joint best over q_lin,q_sq,q_cub,r in [-2,2] (fitted diagnostic) ---\n");
    int best=1<<30; P5 bp;
    for(int a=-2;a<=2;a++)for(int b=-2;b<=2;b++)for(int c=-2;c<=2;c++)for(int r=-2;r<=2;r++){
        P5 p=base; p.qlin=a;p.qsq=b;p.qcub=c;p.rfb=r; int s=w5_score(p); if(s<best){best=s;bp=p;} }
    printf("  best %d at qlin=%+d qsq=%+d qcub=%+d r=%+d   (baseline 294)\n",best,bp.qlin,bp.qsq,bp.qcub,bp.rfb);
    return 0; }
