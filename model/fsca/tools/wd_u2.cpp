// wd_u2.cpp -- part (c): is u2_n = 32+k (k = leading-zero octaves of sin) forced by the data?
// Per table, try u2 = 32+k+d with S rescaled by 2^d (keeping the product scale), plus the node-16
// clamp question (S=2^23-1 vs 2^23 vs a 24-bit field).
#include "wd_core.h"
static int freeBound(Model&M,int&infeas){ build_thr(M); int tot=0; infeas=0;
    for(int t=-512;t<=511;t++){ std::vector<std::pair<i64,i64>> iv; int nc=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e); i64 b=base_of(M,n,t,flip);
            if(!M.S[n]){ if(b<l||b>h) tot++; continue; }
            nc++; i64 aa,bb; if(jint(M,n,b-h,b-l,aa,bb)) iv.push_back({aa,bb}); }
        std::vector<std::pair<i64,int>> ev; for(auto&p:iv){ev.push_back({p.first,1});ev.push_back({p.second+1,-1});}
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; for(auto&e2:ev){cur+=e2.second; if(cur>best)best=cur;}
        tot+=nc-best; if(best<nc) infeas++; }
    return tot; }
int main(){ if(!load_fsca(FF))return 1;
    Cfg c0; c0.i2K=3; c0.i2Kv=1ll<<19; c0.s2v=18; c0.u2base=32;
    Model B; B.c=c0; build_model(B); fit_C0(B);
    printf("baseline: u2 = 32+k, S = RN(sin*2^(23+k)) in [2^22,2^23)\n");
    printf("n   k  S(23b)    u2 | d=-2      d=-1      d=0       d=+1      d=+2     (free-latch bound / infeasible t)\n");
    for(int n=1;n<=16;n++){
        i128 a=PI_()*n/32,s=sin_(a); int k=0; while(s*((i128)1<<(k+1))<QONE) k++;
        printf("%2d  %d  %8lld  %2d |",n,k,(long long)B.S[n],B.u2[n]);
        for(int d=-2;d<=2;d++){ Cfg c=c0; for(int m=0;m<=16;m++) c.C0ov[m]=B.C0[m];
            i64 S = rn_scale(s, 23+k+d); if(S<=0||S>=(1ll<<25)){ printf("  ---      "); continue; }
            c.Sov[n]=S; c.u2ov[n]=32+k+d;
            Model M; M.c=c; build_model(M); for(int m=0;m<=16;m++)M.C0[m]=c.C0ov[m]; M.C0[0]=0;
            int inf; int fb=freeBound(M,inf); printf("  %5d/%-4d",fb,inf); }
        printf("\n"); }
    // node 16 clamp
    printf("\nnode 16 field: sin = 1 exactly, RN(sin*2^23) = 2^23 = %d needs 24 bits\n",1<<23);
    for(i64 S : {(1ll<<23)-1, 1ll<<23, (1ll<<24)-1, 1ll<<24}){
        for(int u=31;u<=33;u++){ Cfg c=c0; for(int m=0;m<=16;m++) c.C0ov[m]=B.C0[m]; c.Sov[16]=S; c.u2ov[16]=u;
            Model M; M.c=c; build_model(M); for(int m=0;m<=16;m++)M.C0[m]=c.C0ov[m]; M.C0[0]=0;
            int inf; int fb=freeBound(M,inf);
            printf("  S_16=%-9lld u2=%d -> free bound %5d, infeasible t %4d%s\n",(long long)S,u,fb,inf,
                (fb==0)?"   <= consistent":""); } }
    return 0; }
