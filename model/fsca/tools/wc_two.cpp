// wc_two.cpp -- final end-to-end numbers for the two variants this session singles out.
#include "wc_core.h"
int main(){ wc_init();
    struct V{const char*n; int ro0,c1,r1;};
    V vs[]={{"baseline           I1=(arr(KC,dx)+K)>>23 ; J=(arr(I1,dx)+K)>>20",0,1,0},
            {"guard-clean best   I1=(arr(dx,KC)+K)>>23 ; J=(arr(I1,dx)+K)>>20",1,1,1},
            {"best jwin          I1=(arr(KC,dx)+K)>>23 ; J= arr(I1,dx)    >>20",0,0,0}};
    for(auto&v:vs){ int per[32]={0},g=0;
        int tot=wc_score([&](int nd,int t)->i64{ int n=nd>=17?32-nd:nd; if(!TB[n].S)return 0;
            i64 dx=(i64)t<<13;
            i64 I1=wlatch((v.ro0?arr(dx,(i64)KC):arr((i64)KC,dx))+K,23,0);
            i64 J =wlatch(arr(I1,dx)+(v.c1?K:0),20,v.r1);
            return sm(arr(J,TB[n].S)+K,TB[n].u2); },per,&g);
        printf("%s\n   total=%d guard(0,1,2,16)=%d  nodes:",v.n,tot,g);
        for(int i=0;i<32;i++) if(per[i]) printf(" %d:%d",i,per[i]); puts("\n"); }
    puts("derived per-node square coefficients (not used by the winner, recorded for reuse):");
    printf("  C2_econ = RN(sin(n*pi/32)*KC*2*2^j) normalised to [2^22,2^23), unit column u = 25+j\n  {");
    for(int n=0;n<=16;n++) printf("%lld%s",(long long)CF[2][n].v,n<16?",":"}  u={");
    for(int n=0;n<=16;n++) printf("%d%s",CF[2][n].ub,n<16?",":"}\n");
    return 0; }
