// b6.cpp -- (1) check that T2 is monotone in the latch I2, (2) build the per-t allowed-I2 interval dataset
// by intersecting all 31 independent node instances, (3) write it to i2win.txt for formula searches.
#include "base2.h"
#include <cstring>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static inline i64 T2of(i64 I2,i64 S,int u){ return sm(arr(I2,S)+K,u); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    // (1) monotonicity of T2 in I2 for every table sine
    int nonmono=0;
    for(int n=1;n<=16;n++){ i64 prev=T2of(0,TB[n].S,U2TAB[n]);
        for(i64 v=1;v<=12000000;v++){ i64 c=T2of(v,TB[n].S,U2TAB[n]); if(c<prev){ if(nonmono<5) printf("non-monotone: n=%d I2=%lld %lld->%lld\n",n,(long long)v,(long long)prev,(long long)c); nonmono++; } prev=c; } }
    printf("monotonicity violations of T2(I2) over I2=0..12e6, all 16 sines: %d\n",nonmono);
    // (2) intervals by binary search (needs monotonicity)
    FILE*f=fopen("i2win.txt","w");
    int empty=0,need=0,tight=0;
    for(int t=-512;t<=511;t++){
        i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, I2m=(arr(I1,dx)+K)>>20;
        i64 gl=0, gh=12000000;                      // global allowed interval for the latch
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo;
            // smallest I2 with T2 >= t2lo
            i64 a1=0,b1=12000001; while(a1<b1){ i64 m=(a1+b1)/2; if(T2of(m,T.S,U2TAB[n])>=t2lo) b1=m; else a1=m+1; }
            // largest I2 with T2 <= t2hi
            i64 a2=-1,b2=12000000; while(a2<b2){ i64 m=(a2+b2+1)/2; if(T2of(m,T.S,U2TAB[n])<=t2hi) a2=m; else b2=m-1; }
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        if(gl>gh){empty++; fprintf(f,"%d EMPTY %lld\n",t,(long long)I2m); continue;}
        if(!(gl<=I2m&&I2m<=gh))need++;
        if(gh-gl<=2)tight++;
        fprintf(f,"%d %lld %lld %lld\n",t,(long long)gl,(long long)gh,(long long)I2m);
    }
    fclose(f);
    printf("wrote i2win.txt: empty %d, model-outside %d, tight(width<=3) %d\n",empty,need,tight);
    return 0; }
