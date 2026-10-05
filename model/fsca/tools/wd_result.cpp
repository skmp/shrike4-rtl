// wd_result.cpp -- final consolidated result: baseline vs the corrected square-latch resolution.
#include "wd_core.h"
static void show(const char*name,Cfg c){ Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
    printf("%-46s M1=%3d  freeLatchBound=%d  infeasible_t=%d  latchRuleScore=%d/1024\n",name,R.M1,R.M2,R.M3,R.M4);
    printf("%-46s nodes 0/1/2/16/30/31 violations: %d/%d/%d/%d/%d/%d\n","",R.nodeviol[0],R.nodeviol[1],R.nodeviol[2],R.nodeviol[16],R.nodeviol[30],R.nodeviol[31]);
    printf("%-46s per-node:","");for(int i=0;i<32;i++)if(R.nodeviol[i])printf(" %d:%d",i,R.nodeviol[i]);printf("\n\n"); }
int main(){ if(!load_fsca(FF))return 1;
    puts("FSCA square path -- latch resolution result\n"
         "  dx = t<<13 ; I1 = (arr(KC,dx)+2^19)>>23 ; I2 = (arr(I1,dx)+k2)>>s2 ; T2 = sm(arr(I2,S_n)+2^19, (50-s2)+k_n)\n"
         "  scale law: s2 + u2 = 50 + k ; KC = 0x4EF145 ; S_n = RN(sin*2^(23+k)) in [2^22,2^23)\n");
    { Cfg c; show("BASELINE  s2=20 u2=30+k k2=2^19 (published)",c); }
    { Cfg c; c.i2K=3;c.i2Kv=1ll<<19;c.s2v=19;c.u2base=31; show("s2=19 u2=31+k k2=2^19",c); }
    { Cfg c; c.i2K=3;c.i2Kv=1ll<<19;c.s2v=18;c.u2base=32; show("NEW       s2=18 u2=32+k k2=2^19",c); }
    { Cfg c; c.i2K=3;c.i2Kv=0;       c.s2v=18;c.u2base=32; show("NEW       s2=18 u2=32+k k2=0    (best M1)",c); }
    { Cfg c; c.i2K=3;c.i2Kv=1ll<<19; c.s2v=17;c.u2base=33; show("s2=17 u2=33+k k2=2^19",c); }
    // the t=+-17 resolution, explicitly
    { Cfg c; c.i2K=3;c.i2Kv=1ll<<19;c.s2v=18;c.u2base=32; Model M; M.c=c; build_model(M); fit_C0(M); build_thr(M);
      for(int t : {17,-17}){ i64 gl=0,gh=JMAX;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e); i64 b=base_of(M,n,t,flip);
            i64 aa,bb; jint(M,n,b-h,b-l,aa,bb); if(aa>gl)gl=aa; if(bb<gh)gh=bb; }
        printf("t=%+3d : allowed latch [%lld,%lld]",t,(long long)gl,(long long)gh);
        int v=0; for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 V=base_of(M,n,t,flip)-sq_term(gl,M.S[n],M.u2[n],M.c); if(V<l||V>h)v++; }
        printf("  -> violations over all 32 node instances at that latch value: %d\n",v); } }
    return 0; }
