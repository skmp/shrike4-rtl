// we_E.cpp -- for a chosen granularity W, invert to the required additive constant E in
//     I1 = floor((KC*t + E)/W),   J = (arr(I1,t<<13)+K2)>>s2   with W*2^s2 = 2^30
// E is the first array's truncation offset, (e1+K1)/2^13; in the real chain E = 48-g(t mod 8) in [41,48].
// Prints the required E interval per t and the best coverage when E is a function of t mod M.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int s2=argc>1?atoi(argv[1]):22; i64 K2=argc>2?atoll(argv[2]):(1ll<<19); int dump=argc>3?atoi(argv[3]):0;
    int lw=30-s2; i64 W=1ll<<lw;
    std::vector<i64> EL(1024,1),EH(1024,0); int nemp=0;
    for(int t=-512;t<=511;t++){ if(!JOK[t+512]||t==0)continue; i64 dx=(i64)t<<13;
        int sg=t<0?-1:1; i64 lo=0,hi=1ll<<27;
        auto Jf=[&](i64 m){ return (arr(sg*m,dx)+K2)>>s2; };
        while(lo<hi){ i64 m=lo+(hi-lo)/2; if(Jf(m)>=JLo[t+512])hi=m; else lo=m+1; }
        i64 a=-1,b=-1;
        for(i64 m=std::max((i64)0,lo-32);m<=lo+8192;m++){ i64 j=Jf(m); if(j>=JLo[t+512]&&j<=JHi[t+512]){ if(a<0)a=m; b=m; } else if(a>=0&&j>JHi[t+512]) break; }
        if(a<0){ nemp++; continue; }
        i64 IL=sg*a,IH=sg*b; if(sg<0){ i64 x=IL;IL=IH;IH=x; }
        EL[t+512]=W*IL-(i64)KC*t; EH[t+512]=W*(IH+1)-1-(i64)KC*t; }
    printf("W=2^%d s2=%d K2=%lld : %d t with no feasible I1\n",lw,s2,(long long)K2,nemp);
    i64 gl=1ll<<62,gh=-(1ll<<62);
    for(int t=-512;t<=511;t++){ if(EL[t+512]>EH[t+512])continue; gl=std::min(gl,EL[t+512]); gh=std::max(gh,EH[t+512]); }
    printf("required-E envelope over all t: [%lld,%lld]  (real chain: E=48-g in [41,48], i.e. (e1+2^19)/2^13)\n",(long long)gl,(long long)gh);
    for(int M=1;M<=256;M*=2){ int tot=0; std::vector<i64> bestE(M);
        for(int m=0;m<M;m++){ int bs=-1; i64 be=0;
            // candidate E values: the interval endpoints of the t in this class
            std::vector<i64> cand;
            for(int t=-512;t<=511;t++){ if(((t%M)+M)%M!=m)continue; if(EL[t+512]>EH[t+512])continue; cand.push_back(EL[t+512]); }
            std::sort(cand.begin(),cand.end()); cand.erase(std::unique(cand.begin(),cand.end()),cand.end());
            for(i64 e:cand){ int s=0; for(int t=-512;t<=511;t++){ if(((t%M)+M)%M!=m)continue; if(EL[t+512]>EH[t+512])continue;
                    if(e>=EL[t+512]&&e<=EH[t+512])s++; } if(s>bs){bs=s;be=e;} }
            tot+=bs; bestE[m]=be; }
        printf("  E as a function of t mod %3d : best coverage %4d / %d\n",M,tot,1022-nemp-1);
        if(M<=16){ printf("     E[] ="); for(int m=0;m<M;m++) printf(" %lld",(long long)bestE[m]); puts(""); } }
    if(dump){ puts("# t  EL  EH  (real E=48-g)"); for(int t=-512;t<=511;t++){ if(EL[t+512]>EH[t+512])continue;
        i64 dx=(i64)t<<13; i64 e1=arr(KC,dx)-(i64)KC*dx; i64 g=(-e1/8192)-16;
        printf("%5d %12lld %12lld   %lld\n",t,(long long)EL[t+512],(long long)EH[t+512],(long long)(48-g)); } }
    return 0; }
