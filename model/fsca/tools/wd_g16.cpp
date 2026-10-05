// wd_g16.cpp -- broader chain sweep at s2=18 reporting M1 and the guard nodes, plus a diagnosis
// of which t break node 16 (the node with no odd terms: V = C0_16 - T2).
#include "wd_core.h"
struct R2 { int M1,M2,M3,M4; int nv[32]; };
static R2 run(int s1,i64 k1,i64 k2,int s2,Model&M){
    Cfg c; c.i2K=3; c.i2Kv=k2; c.s2v=s2; c.u2base=50-s2;
    c.i1K = (k1==(1ll<<19))?0 : (k1==0?1:2);   // i1K==2 -> 2^22
    M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
    R2 r; r.M1=R.M1;r.M2=R.M2;r.M3=R.M3;r.M4=R.M4; memcpy(r.nv,R.nodeviol,sizeof(r.nv)); return r; }
int main(){ if(!load_fsca(FF))return 1; Model M;
    printf("s2 k1      k2       M1   M2 M3  M4  | n0 n1 n2 n16 n30 n31 | guard\n");
    for(int s2 : {17,18,19,20}) for(i64 k1 : {0ll,1ll<<19,1ll<<22}) for(i64 k2 : {0ll,1ll<<16,1ll<<17,1ll<<18,1ll<<19,(1ll<<19)+(1ll<<17)}){
        R2 r=run(23,k1,k2,s2,M);
        bool g=!r.nv[0]&&!r.nv[1]&&!r.nv[2]&&!r.nv[16]&&!r.nv[30]&&!r.nv[31];
        printf("%2d %-7lld %-8lld %4d %3d %2d %4d | %2d %2d %2d %3d %3d %3d | %s\n",s2,(long long)k1,(long long)k2,
            r.M1,r.M2,r.M3,r.M4,r.nv[0],r.nv[1],r.nv[2],r.nv[16],r.nv[30],r.nv[31],g?"EXACT":""); }
    // diagnose node 16 at s2=18, k1=k2=2^19
    Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=18; c.u2base=32;
    M.c=c; build_model(M); fit_C0(M); build_thr(M);
    printf("\nnode 16 detail (s2=18,k1=k2=2^19), C0_16=%lld S=%lld u2=%d:\n",(long long)M.C0[16],(long long)M.S[16],M.u2[16]);
    for(int t=-512;t<=511;t++){ int a=(16*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
        i64 b=base_of(M,16,t,0); i64 J=chainJ(t,M.c); i64 V=b-sq_term(J,M.S[16],M.u2[16],M.c);
        if(V<l||V>h){ i64 aa,bb; bool ok=jint(M,16,b-h,b-l,aa,bb);
            printf("  t=%4d win[%lld,%lld] V=%lld  chainJ=%lld  needs J in [%lld,%lld]%s\n",t,(long long)l,(long long)h,
                (long long)V,(long long)J,(long long)aa,(long long)bb,ok?"":" EMPTY"); } }
    return 0; }
