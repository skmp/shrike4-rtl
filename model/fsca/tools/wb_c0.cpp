// wb_c0.cpp -- what is the C0 adder input?  Intersect the two t=0 windows per table entry (nodes n and 32-n)
// and compare with: RN(sin*2^26), TZ(sin*2^26), and the 23-bit normalised field S shifted by its octave (S<<(3-k)).
#include "base2.h"
#include <cstring>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static const i64 C0FIT[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts(" n  k  win(t=0) intersect     RN26      TZ26     S(23b)   S<<(3-k)  fit      sh=3-k  S<<sh in win?");
    for(int n=0;n<=16;n++){
        i64 l1,h1,l2,h2; bool e1,e2; win_of((n*1024)&0xffff,l1,h1,e1);
        if(n>0&&n<16){ win_of(((32-n)*1024)&0xffff,l2,h2,e2); } else {l2=-1ll<<60;h2=1ll<<60;e2=true;}
        i64 lo=l1>l2?l1:l2, hi=h1<h2?h1:h2;
        int k=TB[n].u2-30; i64 S=TB[n].S; int sh=3-k;
        i64 Ssh = sh>=0? (S<<sh) : (S>>(-sh));
        // TZ26
        i128 a=PI_()*n/32; i128 s=sin_(a); i64 tz=(i64)((s<<26)/QONE); if(tz>=(1ll<<26))tz=(1ll<<26)-1;
        printf("%2d %2d  [%9lld,%9lld] %9lld %9lld %8lld %9lld %9lld  %2d   %s\n",n,k,(long long)lo,(long long)hi,
            (long long)TB[n].sin26,(long long)tz,(long long)S,(long long)Ssh,(long long)C0FIT[n],sh,
            (Ssh>=lo&&Ssh<=hi)?"yes":"NO");
    }
    return 0; }
