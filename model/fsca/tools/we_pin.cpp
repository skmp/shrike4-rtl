// we_pin.cpp -- per-node allowed-J intervals at the tight t, to see how strongly each pin is corroborated
// and how sensitive it is to the table sine field S (+-2) and to C0 (+-1).
#include "we_win.h"
#include <cstring>
#include <algorithm>
static void perT(int t,int dS,int dC0){
    printf("t=%4d  (dS=%+d dC0=%+d)\n",t,dS,dC0);
    i64 gl=0,gh=12000000; i64 dx=(i64)t<<13;
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
        const Tab&T=TB[n]; i64 s=flip?-1:1; i64 S=T.S+dS;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 base=C0TAB[n]+dC0+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n];
        i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,S)+K,u)>=t2lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,S)+K,u)<=t2hi)a2=m;else b2=m-1;}
        if(a1>gl)gl=a1; if(a2<gh)gh=a2;
        printf("   n%2d %s [%9lld,%9lld] w=%lld\n",node,ex?"EX":"  ",(long long)a1,(long long)a2,(long long)(a2-a1));
    }
    printf("   => J in [%lld,%lld]%s\n",(long long)gl,(long long)gh, gl>gh?"  EMPTY":""); }
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    int t=argc>1?atoi(argv[1]):342; int dS=argc>2?atoi(argv[2]):0; int dC=argc>3?atoi(argv[3]):0;
    perT(t,dS,dC); return 0; }
