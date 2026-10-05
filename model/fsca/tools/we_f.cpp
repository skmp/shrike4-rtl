// we_f.cpp -- vary the FINAL square multiply (the one that turns the shared latch J into T2) and
// recompute the whole J-window picture for each variant:
//   #feasible t, #|t| forcing D>=1, best even-J score, current-chain score, free-J violation bound.
// Variants: roles {arr(J,S), arr(S,J)}, rounding constant K2, latch rule, and u2 offset.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
struct V{int role,k2i,rule,du;};
static i64 K2C[3]={1ll<<19,0,1ll<<18};
static inline i64 T2of(const V&v,i64 J,i64 S,int u){ i64 P=(v.role? arr(S,J):arr(J,S))+K2C[v.k2i];
    switch(v.rule){case 0: return P>=0?(P>>u):-((~P)>>u); case 1: return P>>u; default: return P>=0?(P>>u):-((-P)>>u);} }
int main(){ if(!load_fsca(F))return 1; build_tables();
    printf("role k2 rule du | feas  forcedD  evenMax  chain  note\n");
    for(int role=0;role<2;role++) for(int k2i=0;k2i<3;k2i++) for(int rule=0;rule<3;rule++) for(int du=-1;du<=1;du++){
        V v{role,k2i,rule,du};
        std::vector<i64> JL(1024),JH(1024); std::vector<char> ok(1024);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+du;
                i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(T2of(v,m,T.S,u)>=t2lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(T2of(v,m,T.S,u)<=t2hi)a2=m;else b2=m-1;}
                if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; }
        int feas=0; for(int i=0;i<1024;i++)feas+=ok[i];
        int forced=0,evenmax=0;
        for(int a=0;a<=512;a++){ int ip=a+512,im=-a+512; bool okp=a<=511&&ok[ip],okm=ok[im];
            if(a==0){evenmax+= ok[512]?1:0; continue;}
            if(okp&&okm){ i64 lo=std::max(JL[ip],JL[im]),hi=std::min(JH[ip],JH[im]);
                if(lo<=hi)evenmax+=2; else {evenmax+=1;forced++;} }
            else if(okp||okm)evenmax+=1; }
        int chain=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 dx=(i64)t<<13;
            i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20; if(J>=JL[t+512]&&J<=JH[t+512])chain++; }
        printf("%4d %2d %4d %2d | %4d  %6d  %6d  %5d\n",role,(int)K2C[k2i],rule,du,feas,forced,evenmax,chain);
    }
    return 0; }
