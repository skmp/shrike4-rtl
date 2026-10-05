#include "wd_core.h"
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int s2=argc>1?atoi(argv[1]):18; int t=argc>2?atoi(argv[2]):17;
    Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=s2; c.u2base=50-s2;
    Model M; M.c=c; build_model(M); fit_C0(M); build_thr(M);
    printf("JMAX=%lld chainJ(%d)=%lld\n",(long long)JMAX,t,(long long)chainJ(t,M.c));
    i64 gl=0,gh=JMAX;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
        int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
        i64 b=base_of(M,n,t,flip); i64 aa,bb; bool ok=jint(M,n,b-h,b-l,aa,bb);
        printf(" node%2d tab%2d T2in[%lld,%lld] -> J[%lld,%lld]%s\n",node,n,(long long)(b-h),(long long)(b-l),
            (long long)aa,(long long)bb,ok?"":" EMPTY");
        if(ok){ if(aa>gl)gl=aa; if(bb<gh)gh=bb; } }
    printf("intersection [%lld,%lld]\n",(long long)gl,(long long)gh);
    for(i64 J=gl-2;J<=gh+2;J++){ int v=0;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 V=base_of(M,n,t,flip)-sq_term(J,M.S[n],M.u2[n],M.c); if(V<l||V>h)v++; }
        printf("  direct viol at J=%lld : %d\n",(long long)J,v); }
    return 0; }
