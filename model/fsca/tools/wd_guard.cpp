// wd_guard.cpp -- at the finer latch, sweep the chain (s1, A, k1, k2, roles) looking for a rule that
// keeps the guard nodes 0,1,2,16,30,31 exact.  Reports every guard-exact hit, best M1 overall.
#include "wd_core.h"
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int s2=argc>1?atoi(argv[1]):18;
    printf("s1 A          k1      k2      r0 r1  M1   M2 M3  M4   guard\n");
    int best=1<<30; char bl[256]=""; int nguard=0;
    for(int s1=21;s1<=24;s1++){ i64 A=(i64)(((__int128)KC<<s1)+ (1ll<<22))/((i64)1<<23);
      for(i64 k1 : {0ll,1ll<<17,1ll<<18,1ll<<19,1ll<<20}) for(i64 k2 : {0ll,1ll<<16,1ll<<17,1ll<<18,1ll<<19})
      for(int r0=0;r0<2;r0++) for(int r1=0;r1<2;r1++){
        Cfg c; c.i2K=3; c.i2Kv=k2; c.s2v=s2; c.u2base=50-s2; c.s1v=s1; c.Av=A; c.r0=r0; c.r1=r1;
        c.i1K = (k1==(1ll<<19))?0 : (k1==0?1:2); if(k1!=0&&k1!=(1ll<<19)) { if(k1==(1ll<<22)) c.i1K=2; else continue; }
        Model M; M.c=c; build_model(M); fit_C0(M); Metrics R=evaluate(M);
        bool g=!R.nodeviol[0]&&!R.nodeviol[1]&&!R.nodeviol[2]&&!R.nodeviol[16]&&!R.nodeviol[30]&&!R.nodeviol[31];
        if(g){ nguard++; printf("%2d %-10lld %-7lld %-7lld %d  %d  %4d %3d %2d %4d  EXACT\n",s1,(long long)A,(long long)k1,(long long)k2,r0,r1,R.M1,R.M2,R.M3,R.M4); }
        if(R.M1<best){ best=R.M1; snprintf(bl,sizeof bl,"s1=%d A=%lld k1=%lld k2=%lld r0=%d r1=%d M1=%d M2=%d M3=%d M4=%d guard=%s",
            s1,(long long)A,(long long)k1,(long long)k2,r0,r1,R.M1,R.M2,R.M3,R.M4,g?"EXACT":"broken"); } } }
    printf("guard-exact configurations found: %d\nbest M1: %s\n",nguard,bl);
    return 0; }
