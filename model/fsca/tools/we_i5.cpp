// we_i5.cpp -- required first-latch interval per t, exactly, for a chosen second stage
//   J = (arr(I1, t<<13) + K2) >> s2 ,  I1 ~ KC*t/W with W = 2^(30-s2).
// Prints the residual of the required I1 against the exact rational KC*t/W, as a fraction of an LSB,
// so the shape of the needed correction is visible.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int s2=argc>1?atoi(argv[1]):20; i64 K2=argc>2?atoll(argv[2]):(1ll<<19); int lim=argc>3?atoi(argv[3]):0;
    int lw=30-s2; i64 W=1ll<<lw;
    printf("# second stage: J=(arr(I1,t<<13)+%lld)>>%d ; W=2^%d ; I1 ~ KC*t/W\n",(long long)K2,s2,lw);
    printf("# t    IL       IH     ideal=KC*t/W      (IL-ideal)/1  (IH-ideal)/1   width  model  |t|%%8\n");
    int nempty=0,nin=0; double mn=1e18,mx=-1e18; int mnt=0,mxt=0;
    long double sumlo=0,sumhi=0; int cnt=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue; i64 dx=(i64)t<<13;
        i64 Imod=(arr(KC,dx)+K)>>(43-s2);
        int sg=t<0?-1:1; i64 lo=0,hi=1ll<<27;
        auto Jf=[&](i64 m){ return (arr(sg*m,dx)+K2)>>s2; };
        if(t==0){ nin++; continue; }
        while(lo<hi){ i64 m=lo+(hi-lo)/2; if(Jf(m)>=JLo[t+512])hi=m; else lo=m+1; }
        i64 a=-1,b=-1;
        for(i64 m=std::max((i64)0,lo-32);m<=lo+4096;m++){ i64 j=Jf(m); if(j>=JLo[t+512]&&j<=JHi[t+512]){ if(a<0)a=m; b=m; } else if(a>=0&&j>JHi[t+512]) break; }
        if(a<0){ nempty++; printf("EMPTY t=%d\n",t); continue; }
        i64 IL=sg*a,IH=sg*b; if(sg<0){ i64 x=IL;IL=IH;IH=x; }
        long double ideal=(long double)KC*t/(long double)W;
        double dl=(double)(IL-ideal), dh=(double)(IH-ideal);
        if(Imod>=IL&&Imod<=IH) nin++;
        if(dl>mx){mx=dl;mxt=t;} if(dh<mn){mn=dh;mnt=t;}
        sumlo+=dl; sumhi+=dh; cnt++;
        if(lim&&std::abs(t)>=lim) printf("%5d %9lld %9lld %15.3f  %+9.3f %+9.3f  %5lld %9lld  %d\n",
            t,(long long)IL,(long long)IH,(double)ideal,dl,dh,(long long)(IH-IL),(long long)Imod,((t%8)+8)%8);
    }
    printf("empty=%d  model-in=%d\n",nempty,nin);
    printf("max of (IL-ideal) = %+.3f at t=%d ; min of (IH-ideal) = %+.3f at t=%d\n",mx,mxt,mn,mnt);
    printf("mean (IL-ideal)=%.3f mean (IH-ideal)=%.3f over %d t\n",(double)(sumlo/cnt),(double)(sumhi/cnt),cnt);
    return 0; }
