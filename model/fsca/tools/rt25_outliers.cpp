// rt25_outliers.cpp -- residual r(t) = J_chain(t) - J_hw(t) in coarse units, J_hw in the coarse window of jwin.txt.
// Lists every offset whose window EXCLUDES the bulk interval [-1, 3), with which ROM words pin each side.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); FILE*f=fopen("jwin.txt","r"); static long JL[1024],JH[1024]; int tt; long a,b,fl; while(fscanf(f,"%d %ld %ld %ld",&tt,&a,&b,&fl)==4){JL[tt+512]=a;JH[tt+512]=b;}
    puts("    t    chain J    window [JL,JH]      r = chain - hw in    lower edge set by word   upper edge set by word     |t| band");
    int nlow=0,nhigh=0,npos_low=0,npos_high=0;
    for(int t=-511;t<=511;t++){ if(!t||JL[t+512]>JH[t+512])continue; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20; long rlo=J-JH[t+512], rhi=J-JL[t+512];
        bool low = rhi < -1, high = rlo >= 3; if(!low&&!high)continue; if(low){nlow++; if(t>0)npos_low++;} else {nhigh++; if(t>0)npos_high++;}
        // find which word pins each edge: per word, the J interval allowed
        int wl=-1,wh=-1; for(int m=1;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; if(!T.S)continue; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0; int ang=(m*1024+t)&0xffff; Win w=decode(F.srz[ang],F.srn[ang]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;}
            auto ok=[&](i64 j){ i64 T2=sm(arr(j,T.S)+K,T.u2); i64 V=C0TAB[n]+s*T1-s*T3-T2; return V>=lo&&V<=hi; };
            if(ok(JL[t+512])&&!ok(JL[t+512]-1)&&wl<0)wl=m; if(ok(JH[t+512])&&!ok(JH[t+512]+1)&&wh<0)wh=m; }
        printf("%5d %10lld  [%9ld,%9ld]   [%+3ld,%+3ld] %s      %2d                      %2d\n",t,(long long)J,JL[t+512],JH[t+512],rlo,rhi,low?"hw ABOVE chain":"hw BELOW chain",wl,wh); }
    printf("hardware above the chain by >= 2: %d offsets (%d at t>0) | hardware below the chain by >= 3: %d offsets (%d at t>0)\n",nlow,npos_low,nhigh,npos_high);
    return 0; }
