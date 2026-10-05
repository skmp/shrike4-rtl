#include "wa_common.h"
static Fsca F;
static const i64 C0B[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2B[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    i64 K1=atoll(argv[1]),K2=atoll(argv[2]);
    Cfg c{0x4ef145,13,23,0,0,0,0,13,20,0,0};
    int gn[6]={0,1,2,16,30,31};
    for(int gi=0;gi<6;gi++){ int m=gn[gi]; int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1; int v=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 J=Jof(c,K1,K2,t); i64 T2=T.S? sm(arr(J,T.S)+K,U2B[n]):0;
            i64 V=C0B[n]+s*T1-T2-s*T3; int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            if(V<lo||V>hi){ if(v<3) printf("  node %2d t=%4d V=%lld win[%lld,%lld] J=%lld\n",m,t,(long long)V,(long long)lo,(long long)hi,(long long)J); v++; } }
        printf("node %2d: %d violations (fixed C0/u2)\n",m,v); }
    return 0; }
