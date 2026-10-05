// w5_b_msb.cpp -- BRANCH 5, HYPOTHESIS B(ii): t has an extra MSB, i.e. the node split is COARSER
// (2048 angles per node instead of 1024).  Model-free disproof by a 4th-difference certificate:
//   for ANY cubic p,  p(x-2h) - 4p(x-h) + 6p(x) - 4p(x+h) + p(x+2h) == 0.
// The dump pins each value to a window, so the achievable interval of that combination is computable
// in integers.  If it excludes 0, no cubic in t can fit the node, whatever the coefficient rules.
// The same combination lower-bounds the minimax error of the best cubic by |D| / 16.
// Also: which side of the node boundary each boundary angle actually belongs to (rho = 511 vs 512 vs 513).
#include "w5_model.h"
#include <cstdio>
#include <algorithm>
static inline void winA(int a,i64&lo,i64&hi){ int m=a&0xffff; w5_win(m,lo,hi); }
// achievable interval of the 4th difference at centre c with step h (all in ANGLE units)
static void d4(int c,int h,i64&dmin,i64&dmax){
    i64 l[5],hh[5]; for(int k=0;k<5;k++) winA(c+(k-2)*h,l[k],hh[k]);
    const int w[5]={1,-4,6,-4,1}; dmin=0;dmax=0;
    for(int k=0;k<5;k++){ if(w[k]>0){dmin+=w[k]*l[k]; dmax+=w[k]*hh[k];} else {dmin+=w[k]*hh[k]; dmax+=w[k]*l[k];} } }
int main(){ if(!load_fsca(W5F))return 1; build_tables();
    printf("--- B(ii): can a cubic in t fit a node at all?  4th-difference certificate, model-free ---\n");
    printf("(units of 2^-26; the dump pins each sample to a window of width 0..1, so the combination's\n");
    printf(" own slack is at most 16 units.  |D| is what a cubic must make ZERO.)\n\n");
    for(int span : {1024,2048,4096}){
        int h = span/4;                      // 5 points spanning the whole node width
        i64 worst=0; int worstc=0; int nexcl=0, ntot=0;
        for(int c=0;c<65536;c+=span){ i64 dmin,dmax; d4(c,h,dmin,dmax); ntot++;
            i64 m = dmin>0? dmin : (dmax<0? -dmax : 0);
            if(dmin>0||dmax<0) nexcl++;
            if(m>worst){worst=m;worstc=c;} }
        printf("  node span %4d angles (h=%3d): %2d/%2d node centres where NO cubic exists;\n",
            span,h,nexcl,ntot);
        printf("      worst |D| = %lld at centre %d  ->  best cubic's max error >= %lld/16 = %.2f units\n",
            (long long)worst,worstc,(long long)worst,(double)worst/16.0);
    }
    printf("\n  the FSCA model's own budget: C0 deviations from RN(sin*2^26) are 0..-4 units, and the\n");
    printf("  model has no quartic term at all.\n");
    printf("\n--- the node boundary: which node does angle = 1024n + 512 belong to? ---\n");
    P5 p512; P5 p511=p512; p511.rho=511; P5 p513=p512; p513.rho=513;
    // count violations restricted to the 64 boundary angles a = 1024n+512
    auto bviol=[&](const P5&P,int off)->int{ int v=0;
        for(int n=0;n<64;n++){ int a=(1024*n+off)&0xffff;
            int nr=(a+P.rho)>>10; int node=nr&63; int t=a-1024*nr;
            int so=1,m=node; if(m>=32){so=-1;m-=32;} int flip=m>=17,nn=flip?32-m:m;
            i64 V=so*w5_V(nn,t,flip,P); i64 lo,hi; w5_win(a,lo,hi); if(V<lo||V>hi)v++; }
        return v; };
    printf("  angles a = 1024n+512 (64 of them):  rho=512 (t=-512, upper node) %d violations ;"
           "  rho=511 (t=+512, lower node) %d violations\n",bviol(p512,512),bviol(p511,512));
    printf("  angles a = 1024n+511 (64 of them):  rho=512 (t=+511) %d violations ;"
           "  rho=513 (t=-513, upper node) %d violations\n",bviol(p512,511),bviol(p513,511));
    printf("  full-model totals: rho=511 %d, rho=512 %d, rho=513 %d\n",
        w5_score(p511),w5_score(p512),w5_score(p513));
    return 0; }
