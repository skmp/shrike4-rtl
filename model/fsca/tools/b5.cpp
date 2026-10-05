// b5.cpp -- per-node allowed I2 interval at a given t (diagnose conflicts), for each C0 choice in its window.
#include "base2.h"
#include <cstring>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int t=argc>1?atoi(argv[1]):17; int RAD=argc>2?atoi(argv[2]):64;
    i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, I2=(arr(I1,dx)+K)>>20;
    printf("t=%d  I1=%lld I2=%lld  ideal I2 = %.4Lf\n",t,(long long)I1,(long long)I2,(long double)KC*t*t/131072.0L);
    printf("node tab flip  S        u2  C0        win               T1      T3     allowed I2 (offsets from model)\n");
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
        const Tab&T=TB[n]; i64 s=flip?-1:1;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        // include both C0 candidates in the t=0 window
        i64 lo0,hi0;bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
        printf("%3d %3d  %d  %06llx %2d %9lld [%9lld,%9lld]%s %7lld %6lld  ",node,n,flip,(long long)T.S,U2TAB[n],
            (long long)C0TAB[n],(long long)lo,(long long)hi,ex?"*":" ",(long long)(s*T1),(long long)(s*T3));
        for(i64 c=lo0;c<=hi0;c++){
            i64 base=c+s*T1-s*T3; i64 t2lo=base-hi,t2hi=base-lo; int d1=RAD+1,d2=-RAD-1;
            for(int d=-RAD;d<=RAD;d++){ i64 T2=sm(arr(I2+d,T.S)+K,U2TAB[n]); if(T2>=t2lo&&T2<=t2hi){ if(d<d1)d1=d; d2=d; } }
            if(d1>d2) printf("C0=%lld:none ",(long long)c); else printf("C0=%lld:[%+d,%+d] ",(long long)c,d1,d2);
        }
        printf("\n"); }
    return 0; }
