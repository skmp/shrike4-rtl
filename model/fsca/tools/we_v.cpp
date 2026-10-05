// we_v.cpp -- validate the J windows: which node binds each side at the forced-D and pinned |t|,
// and how wide the t=0 windows are (C0 is measured there, so a narrow t=0 window means C0 is not a fit).
#include "we_win.h"
#include <cstring>
#include <algorithm>
#include <map>
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    puts("node  t=0 window            width  C0TAB  RN26   u2  #exact-windows(1024)");
    for(int n=0;n<=16;n++){ i64 lo,hi;bool ex; win_of(n*1024,lo,hi,ex);
        int nex=0; for(int t=-512;t<=511;t++){ i64 l,h;bool e; win_of((n*1024+t)&0xffff,l,h,e); if(e)nex++; }
        printf("%3d  [%9lld,%9lld] %5lld  %9lld %9lld %3d  %4d%s\n",n,(long long)lo,(long long)hi,(long long)(hi-lo),
            (long long)C0TAB[n],(long long)TB[n].sin26,U2TAB[n],nex, (C0TAB[n]<lo||C0TAB[n]>hi)?"   <-- C0 OUTSIDE t=0 window!":"");
    }
    // binding nodes
    auto bound=[&](int t,i64&gl,i64&gh,int&nlo,int&nhi){ gl=0;gh=12000000;nlo=nhi=-1;
        for(auto&c:CONS[t+512]){
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,c.S)+K,c.u)>=c.lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,c.S)+K,c.u)<=c.hi)a2=m;else b2=m-1;}
            if(a1>gl){gl=a1;nlo=c.node;} if(a2<gh){gh=a2;nhi=c.node;} } };
    puts("\n|t| forced-D cases: binding nodes (lo/hi) on each side");
    printf("%4s %26s %26s\n","a","+a: [JL(node) , JH(node)]","-a: [JL(node) , JH(node)]");
    std::map<int,int> cnt;
    int forced[]={86,148,179,189,194,197,201,209,233,249,251,255,258,277,285,299,308,324,325,332,342,344,359,383,385,396,401,408,411,412,415,417,431,444,447,455,456,458,459,469,476,481,487,494,499,40,376};
    for(int a: forced){ i64 l1,h1,l2,h2; int nl1,nh1,nl2,nh2;
        bound(a,l1,h1,nl1,nh1); bound(-a,l2,h2,nl2,nh2);
        printf("%4d  %9lld(n%2d) %9lld(n%2d)   %9lld(n%2d) %9lld(n%2d)\n",a,(long long)l1,nl1,(long long)h1,nh1,(long long)l2,nl2,(long long)h2,nh2);
        cnt[nl1]++;cnt[nh1]++;cnt[nl2]++;cnt[nh2]++; }
    printf("binding-node histogram:"); for(auto&p:cnt)printf(" n%d:%d",p.first,p.second); puts("");
    return 0; }
