// b11.cpp -- invert the SECOND chain multiply: for each candidate (s1,s2,roles,K2), compute the allowed
// interval for the first latch I1 per t, then test whether a clean I1 rule fits.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static char OKT[1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl;JH[t+512]=gh;OKT[t+512]=gl<=gh; }
    // enumerate second-stage variants; for each, allowed I1 interval per t
    printf("s1 s2 r2 K2  | t with non-empty I1 set | best I1-rule hits (rule: floor((arr(KC,dx)+K1)>>s1))\n");
    for(int s1=14;s1<=30;s1++){ int s2=43-s1; if(s2<1)continue;
      for(int r2=0;r2<2;r2++) for(int k2=0;k2<3;k2++){
        i64 K2 = k2==0?0 : k2==1?(1ll<<19) : (1ll<<(s2-1));
        int nonempty=0; std::vector<i64> il(1024),ih(1024); std::vector<char> ok2(1024,0);
        for(int t=-512;t<=511;t++){ if(!OKT[t+512]) continue; i64 dx=(i64)t<<13;
            // I1 magnitude scale: |I1| ~ KC*|t|*2^13/2^s1 ; scan a generous window around it
            long double c = (long double)KC*(long double)t*8192.0L/powl(2.0L,(long double)s1);
            i64 lo=(i64)floorl(c)-4000, hi=(i64)ceill(c)+4000; i64 f=0,g=0; bool any=false;
            for(i64 v=lo;v<=hi;v++){ i64 P2 = r2? arr(dx,v) : arr(v,dx); i64 J=(P2+K2)>>s2;
                if(J>=JL[t+512]&&J<=JH[t+512]){ if(!any){f=v;any=true;} g=v; } }
            if(any){ nonempty++; il[t+512]=f; ih[t+512]=g; ok2[t+512]=1; } }
        // test I1 rules
        int best=-1,bk=0;
        for(int k1=0;k1<4;k1++){ i64 K1 = k1==0?0 : k1==1?(1ll<<19) : k1==2?(1ll<<(s1-1)) : (1ll<<(s1>13?s1-2:0));
            int hits=0; for(int t=-512;t<=511;t++){ if(!ok2[t+512])continue; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K1)>>s1;
                if(I1>=il[t+512]&&I1<=ih[t+512]) hits++; }
            if(hits>best){best=hits;bk=k1;} }
        printf("%2d %2d  %d  %d  | %4d                   | %4d (K1 variant %d)\n",s1,s2,r2,k2,nonempty,best,bk);
      } }
    return 0; }
