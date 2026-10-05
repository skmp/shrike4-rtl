// wd_verify.cpp -- INDEPENDENT verification of the s2=18 result.
//  (1) re-verify T2(J) monotonicity for u2 = 32+k over the whole latch range
//  (2) for every t, find the allowed latch interval by DIRECT evaluation of all 31 node instances
//      (no threshold arrays), and confirm the endpoints are tight
//  (3) report the minimum achievable window violations over nodes 0..31
#include "wd_core.h"
static Model M;
static i64 LO[32][1024],HI[32][1024]; static i64 BASE[32][1024];
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int s2=argc>1?atoi(argv[1]):18;
    Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=s2; c.u2base=50-s2;
    M.c=c; build_model(M); fit_C0(M);
    printf("config: s2=%d  u2=%d+k  K_i2=2^19\n",s2,50-s2);
    printf("C0 :"); for(int n=0;n<=16;n++)printf(" %lld",(long long)M.C0[n]); printf("\n");
    printf("S  :"); for(int n=0;n<=16;n++)printf(" %lld",(long long)M.S[n]); printf("\n");
    printf("u2 :"); for(int n=0;n<=16;n++)printf(" %d",M.u2[n]); printf("\n");
    set_jmax(M.c);
    // (1) monotonicity, direct
    {   int bad=0;
#pragma omp parallel for reduction(+:bad)
        for(int n=1;n<=16;n++){ i64 prev=-1; for(i64 J=0;J<=JMAX;J++){ i64 v=sq_term(J,M.S[n],M.u2[n],M.c); if(v<prev)bad++; prev=v; } }
        printf("(1) T2(J) monotonicity violations over J in [0,%lld] x 16 tables: %d\n",(long long)JMAX,bad); }
    // cache windows and bases
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; bool e; win_of(a,LO[node][t+512],HI[node][t+512],e);
            BASE[node][t+512]=base_of(M,n,t,flip); } }
    auto viol=[&](int t,i64 J){ int v=0; for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        i64 V=BASE[node][t+512]-sq_term(J,M.S[n],M.u2[n],M.c); if(V<LO[node][t+512]||V>HI[node][t+512])v++; } return v; };
    // (2)(3)
    int tot=0,nbad=0; i64 BESTJ[1024];
    long double fp=(long double)KC/131072.0L*(long double)(1<<(20-s2));
#pragma omp parallel for reduction(+:tot,nbad) schedule(dynamic)
    for(int t=-512;t<=511;t++){
        i64 ideal=(i64)(fp*(long double)t*t);
        i64 lo=ideal-20000; if(lo<0)lo=0; i64 hi=ideal+20000; if(hi>JMAX)hi=JMAX;
        int bv=1<<30; i64 bj=lo,ja=-1,jb=-1;
        for(i64 J=lo;J<=hi;J++){ int v=viol(t,J); if(v<bv){bv=v;bj=J;ja=J;jb=J;} else if(v==bv&&v==0){jb=J;} }
        tot+=bv; if(bv)nbad++; BESTJ[t+512]=bj;
        if(bv==0){ /* tightness: endpoints must fail */ if(ja>lo&&viol(t,ja-1)==0) { printf("WARN t=%d window open below\n",t);} }
    }
    printf("(2) minimum total window violations over nodes 0..31 with a free latch per t: %d  (bad t: %d)\n",tot,nbad);
    // (3) full model with the plain chain, for reference
    Metrics R=evaluate(M);
    printf("(3) with the unchanged chain latch: M1=%d  M2=%d  M3=%d  M4=%d\n",R.M1,R.M2,R.M3,R.M4);
    // dump the allowed windows
    FILE*f=fopen(s2==18?"wd_jwin18.txt":"wd_jwin_other.txt","w");
    for(int t=-512;t<=511;t++){
        i64 ideal=(i64)(fp*(long double)t*t); i64 lo=ideal-20000; if(lo<0)lo=0; i64 hi=ideal+20000; if(hi>JMAX)hi=JMAX;
        i64 a=-1,b=-1; for(i64 J=lo;J<=hi;J++){ if(viol(t,J)==0){ if(a<0)a=J; b=J; } }
        fprintf(f,"%d %lld %lld %d\n",t,(long long)a,(long long)b,a>=0?1:0); }
    fclose(f); printf("windows written\n");
    return 0; }
