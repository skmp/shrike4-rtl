// wd_tab.cpp -- exact-rational audit of the derived tables: S_n (field, width, rounding direction),
// u2_n (exponent rule), C0_n (t=0 window, RN26 delta, ambiguity), C1f/e1, c3.
#include "base2.h"
#include <cstring>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// print v (Q60) * 2^sh as integer.fraction with 9 decimals
static void show(i128 v,int sh,char*out){ i128 num = sh>=0? (v<<sh) : (v>>(-sh));
    i128 q=num/QONE, r=num%QONE; // fraction r/2^60 -> 9 decimals
    i128 fr = r*1000000000/QONE; sprintf(out,"%lld.%09lld",(long long)q,(long long)fr); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts("n   sin*2^(23+k) exact          S        k u2  | C0 window        RN26(sin) exact sin*2^26   C0  d  ambig");
    for(int n=0;n<=16;n++){
        i128 a=PI_()*n/32; i128 s=sin_(a);
        int k=0; if(n>0){ while(s*((i128)1<<(k+1))<QONE) k++; }
        char b1[64],b2[64]; show(s,23+k,b1); show(s,26,b2);
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        printf("%2d  %-24s %8lld  %d %2d  | [%8lld,%8lld]%s %8lld  %-20s %8lld %+3lld %s\n",
            n,b1,(long long)TB[n].S,k,TB[n].u2,(long long)lo0,(long long)hi0,ex0?"*":" ",
            (long long)TB[n].sin26,b2,(long long)TB[n].C0,(long long)(TB[n].C0-TB[n].sin26),
            hi0>lo0?"AMBIG":"");
    }
    puts("");
    puts("n   C1f      e1  c3     | cos*pi/2*2^23 exact (unnormalised)   cos*2646 exact");
    for(int n=0;n<=16;n++){ i128 a=PI_()*n/32; i128 c=cos_(a);
        char b1[64],b2[64]; i128 v=c*PI_()/QONE*((i128)1<<22); show(v,0,b1);
        i128 cu=c*PI_()/QONE; cu=cu*PI_()/QONE; cu=cu*PI_()/QONE; cu=cu*512/6; show(cu,0,b2);
        printf("%2d  %7lld  %2d  %5lld  | %-28s %s\n",n,(long long)TB[n].C1f,TB[n].e1,(long long)TB[n].c3,b1,b2); }
    return 0; }
