// b8.cpp -- structural probes:
//  (a) is arr(dx,dx) exactly t^2*2^26 ?  (does the squaring multiply itself carry a sign asymmetry?)
//  (b) how often are the allowed latch intervals at +t and -t DISJOINT ?  (must the latch be odd-ish in t?)
#include "base2.h"
#include <cstring>
#include <vector>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    // (a)
    int bad=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 p=arr(dx,dx); i64 want=(i64)t*t<<26;
        if(p!=want){ if(bad<12) printf("arr(dx,dx) != t^2*2^26 at t=%d : %lld vs %lld (diff %lld)\n",t,(long long)p,(long long)want,(long long)(p-want)); bad++; } }
    printf("(a) arr(dx,dx) mismatches vs exact t^2*2^26: %d/1024\n\n",bad);
    // (b)
    std::vector<i64> JL(1024),JH(1024); std::vector<char> ok(1024,1);
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            i64 a1=0,b1=12000001; while(a1<b1){ i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo) b1=m; else a1=m+1; }
            i64 a2=-1,b2=12000000; while(a2<b2){ i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi) a2=m; else b2=m-1; }
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; }
    int disj=0,both=0; printf("(b) |t| where the +t and -t allowed latch intervals are DISJOINT:\n");
    for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512; if(!ok[ip]||!ok[im])continue; both++;
        i64 lo=std::max(JL[ip],JL[im]), hi=std::min(JH[ip],JH[im]);
        if(lo>hi){ disj++; printf("  |t|=%3d : +[%lld,%lld] -[%lld,%lld]  gap %lld\n",a,(long long)JL[ip],(long long)JH[ip],(long long)JL[im],(long long)JH[im],(long long)(lo-hi-1)); } }
    printf("(b) disjoint %d of %d comparable |t|\n",disj,both);
    return 0; }
