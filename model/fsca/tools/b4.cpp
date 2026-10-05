// b4.cpp -- exact allowed interval for the shared latch I2, per t, intersected over all 31 node instances.
// Prints it next to the ideal (infinite-precision) chain values so the true rounding rule can be read off.
#include "base2.h"
#include <cstring>
#include <vector>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static const int RAD=256;
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int only_bad = argc>1 && !strcmp(argv[1],"bad");
    puts("   t   I2model  allowed_dI2   ->  allowed I2 range     ideal KC*t^2/2^17   I1model  ideal KC*t/2^10");
    int nempty=0, nneed=0;
    for(int t=-512;t<=511;t++){
        i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, I2=(arr(I1,dx)+K)>>20;
        int dmin=RAD+1,dmax=-RAD-1; std::vector<char> ok(2*RAD+1,1);
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo;
            for(int d=-RAD;d<=RAD;d++){ if(!ok[d+RAD])continue; i64 T2=sm(arr(I2+d,T.S)+K,U2TAB[n]); if(T2<t2lo||T2>t2hi) ok[d+RAD]=0; } }
        for(int d=-RAD;d<=RAD;d++) if(ok[d+RAD]){ if(d<dmin)dmin=d; dmax=d; }
        bool empty = dmin>dmax, need = !empty && !(dmin<=0&&0<=dmax);
        if(empty)nempty++; if(need)nneed++;
        if(only_bad && !empty && !need) continue;
        // ideal values
        long double e2 = (long double)KC*(long double)t*(long double)t/131072.0L;
        long double e1 = (long double)KC*(long double)t/1024.0L;
        if(empty) printf("%5d %9lld  EMPTY                                 %18.4Lf %9lld %14.4Lf\n",t,(long long)I2,e2,(long long)I1,e1);
        else printf("%5d %9lld  [%+4d,%+4d]%s  [%9lld,%9lld] %18.4Lf %9lld %14.4Lf\n",t,(long long)I2,dmin,dmax,
                need?" NEED":"     ",(long long)(I2+dmin),(long long)(I2+dmax),e2,(long long)I1,e1);
    }
    printf("\nempty %d, need-nonzero %d\n",nempty,nneed);
    return 0; }
