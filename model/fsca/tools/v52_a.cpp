// v52_a.cpp -- hypothesis A: t computed 2s, consumed 1s (and converse).  Square path on jwin18 + full model.
#include "v52_common.h"
static Fsca F;
static JW W;
// generalised chain: separate t-modification per operand placement, both latch conventions
static inline i64 lat(i64 P,int s,int conv){ return conv? sm(P,s) : (P>>s); }
static i64 chain(int t,int da,int db,int r,int c1,int c2){
    int ta = t + (t<0?da:0), tb = t + (t<0?db:0);
    i64 dxa=(i64)ta<<13, dxb=(i64)tb<<13;
    i64 I1 = lat(arr(KC,dxa)+K,23,c1) + r;
    return lat(arr(I1,dxb)+K,18,c2);
}
int main(){ if(!load_fsca(F)||!load_jwin(W)){puts("load fail");return 1;} build_tables();
    i64 J[1024];
    puts("== A: square path on jwin18 (baseline 918) ==");
    for(int d=-4;d<=4;d++){ for(int t=-512;t<=511;t++) J[t+512]=chain(t,d,d,0,0,0);
        printf("  d=%+d (both operands, t<0 -> t+d) : %d/1024\n",d,jscore(W,J)); }
    puts("== A: rigidity for t>=0, measured over the whole placement family ==");
    { i64 ref[512]; for(int t=0;t<=511;t++) ref[t]=baseJ(t);
      long long cfgs=0; int bad=0, best=-1; int bd_a=0,bd_b=0,bd_r=0;
      for(int da=-4;da<=4;da++)for(int db=-4;db<=4;db++)for(int r=-16;r<=16;r++)for(int c1=0;c1<2;c1++)for(int c2=0;c2<2;c2++){
        cfgs++; int sc=0;
        for(int t=-512;t<=511;t++){ i64 v=chain(t,da,db,r,c1,c2); J[t+512]=v;
            if(t>=0 && r==0 && v!=ref[t]) bad++; }
        sc=jscore(W,J); if(sc>best){best=sc;bd_a=da;bd_b=db;bd_r=r;} }
      printf("  configs scanned: %lld ; t>=0 deviations from baseline at r=0: %d\n",cfgs,bad);
      printf("  best score over the family: %d/1024 at da=%d db=%d r=%d\n",best,bd_a,bd_b,bd_r); }
    puts("== A: ceiling.  Perfect negative half + actual positive half ==");
    { int pos=0; for(int t=0;t<=511;t++){ i64 v=baseJ(t); if(v>=W.lo[t+512]&&v<=W.hi[t+512])pos++; }
      printf("  positive half is invariant at %d/512 -> any t<0-only hypothesis caps at %d/1024\n",pos,pos+512); }
    puts("== A: sign-magnitude reading value = -(2^(n-1)+t) for t<0 ==");
    for(int n=10;n<=16;n++){ for(int t=-512;t<=511;t++){ int te = t<0 ? -((1<<(n-1))+t) : t;
        i64 dx=(i64)te<<13; J[t+512]=chainJ_dx(dx,dx); }
        printf("  n=%d : %d/1024\n",n,jscore(W,J)); }
    puts("== non-sign-dependent: missing carry-in, uniform t-1 ==");
    { for(int t=-512;t<=511;t++){ i64 dx=(i64)(t-1)<<13; J[t+512]=chainJ_dx(dx,dx);} printf("  uniform t-1 : %d/1024\n",jscore(W,J)); }
    puts("== A: full model over 65536 angles (baseline 294 at order=1 / 296 at order=0) ==");
    for(int o=0;o<2;o++) for(int d=-1;d<=1;d++){ Cfg c; c.order=o; c.tdelta_neg=d; FullRes r=full_score(c,F);
        printf("  order=%d d=%+d : %d violations (pos %d neg %d)\n",o,d,r.viol,r.viol_pos,r.viol_neg); }
    { Cfg c; c.order=1; c.tdelta_all=-1; FullRes r=full_score(c,F); printf("  order=1 uniform t-1 : %d violations\n",r.viol); }
    return 0; }
