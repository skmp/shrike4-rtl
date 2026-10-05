#include "v52_common.h"
static Fsca F;
int main(){
    if(!load_fsca(F)){puts("no dump");return 1;} build_tables();
    JW W; if(!load_jwin(W)){puts("no jwin18");return 1;}
    // contract self-check: b39's own claims about the file
    int feas=0,tight=0; long long sumw=0;
    for(int i=0;i<1024;i++){ if(W.lo[i]<=W.hi[i]){feas++;sumw+=W.hi[i]-W.lo[i]+1; if(W.hi[i]-W.lo[i]<=3)tight++;} }
    printf("jwin18 contract: %d/1024 feasible, %d width<=4, mean width %.1f\n",feas,tight,(double)sumw/feas);
    i64 J[1024]; for(int t=-512;t<=511;t++) J[t+512]=baseJ(t);
    printf("BASELINE jwin18 score = %d/1024\n", jscore(W,J));
    int pos=0,neg=0; for(int t=0;t<=511;t++) if(J[t+512]>=W.lo[t+512]&&J[t+512]<=W.hi[t+512])pos++;
    for(int t=-512;t<0;t++) if(J[t+512]>=W.lo[t+512]&&J[t+512]<=W.hi[t+512])neg++;
    printf("  split: pos t>=0 %d/512   neg t<0 %d/512\n",pos,neg);
    printf("  J(17)=%lld  win[%lld,%lld]\n",(long long)J[17+512],(long long)W.lo[17+512],(long long)W.hi[17+512]);
    printf("  J(342)=%lld  win[%lld,%lld]\n",(long long)J[342+512],(long long)W.lo[342+512],(long long)W.hi[342+512]);
    printf("  J(-342)=%lld win[%lld,%lld]\n",(long long)J[-342+512],(long long)W.lo[-342+512],(long long)W.hi[-342+512]);
    // operand-order variant arr(dx,KC)
    i64 J2[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23; J2[t+512]=(arr(I1,dx)+K)>>18; }
    int p2=0,n2=0; for(int t=0;t<=511;t++) if(J2[t+512]>=W.lo[t+512]&&J2[t+512]<=W.hi[t+512])p2++;
    for(int t=-512;t<0;t++) if(J2[t+512]>=W.lo[t+512]&&J2[t+512]<=W.hi[t+512])n2++;
    printf("  arr(dx,KC) order: %d/1024 (pos %d neg %d)\n",jscore(W,J2),p2,n2);
    Cfg c; FullRes r=full_score(c,F);
    printf("FULL MODEL baseline: %d violations over 65536 angles (t>=0: %d, t<0: %d)\n",r.viol,r.viol_pos,r.viol_neg);
    return 0; }
