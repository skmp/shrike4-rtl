// b41.cpp -- print one model-free asymmetry witness in full, so it can be checked by hand.
#include "base2.h"
#include <cstring>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int node=argc>1?atoi(argv[1]):9, A=argc>2?atoi(argv[2]):86;
    int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
    printf("witness: node %d (table %d%s), |t| = %d\n",node,n,flip?", cos sign flipped":"",A);
    printf("  C1f=%06llx e1=%d  c3=%03llx\n",(long long)TB[n].C1f,TB[n].e1,(long long)TB[n].c3);
    i64 il=-(1ll<<60), ih=(1ll<<60);
    for(int sg=0;sg<2;sg++){ int t=sg?-A:A; i64 dx=(i64)t<<13;
        i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
        i64 O=s*T1-s*T3; int ang=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(ang,lo,hi,ex);
        printf("  t = %+5d : angle %5d  float RN %08x RZ %08x -> internal window [%lld,%lld]%s\n",
            t,ang,F.srn[ang],F.srz[ang],(long long)lo,(long long)hi,ex?" (exact)":"");
        printf("             odd part s*(T1-T3) = %+lld  ->  even part must be in [%lld, %lld]\n",
            (long long)O,(long long)(lo-O),(long long)(hi-O));
        if(lo-O>il) il=lo-O; if(hi-O<ih) ih=hi-O; }
    printf("  intersection: [%lld, %lld]  ->  %s\n",(long long)il,(long long)ih,
        il>ih? "EMPTY: no single even part satisfies both signs of t":"non-empty");
    return 0; }
