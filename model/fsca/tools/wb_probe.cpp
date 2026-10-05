// wb_probe.cpp -- per-node J constraints at a given t: which node pins the window, and how hard.
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int ai=1;ai<argc;ai++){ int t=atoi(argv[ai]); i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23, Jc=(arr(I1,dx)+K)>>20; i64 q=(i64)KC*t*t>>17;
        printf("\n=== t=%d   chain J=%lld  exact floor=%lld (frac %.4f) ===\n",t,(long long)Jc,(long long)q,
            (double)((i64)KC*t*t&((1<<17)-1))/131072.0);
        puts("node tab  Vwin(lo..hi) ex   T1        T3       T2lo   T2hi  | Jlo      Jhi     width");
        i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 base=C0FIT[n]+s*T1-s*T3; i64 t2lo=base-hi,t2hi=base-lo; int u=U2FIT[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2;
            printf("%3d %3d %10lld..%-10lld %d %9lld %8lld %7lld %6lld | %8lld %8lld %6lld %s\n",node,n,
                (long long)lo,(long long)hi,(int)ex,(long long)T1,(long long)T3,(long long)t2lo,(long long)t2hi,
                (long long)a1,(long long)a2,(long long)(a2-a1), (a2-a1)<400?"<== tight":""); }
        printf("intersection: [%lld,%lld]  (chain %lld, exact %lld)\n",(long long)gl,(long long)gh,(long long)Jc,(long long)q);
    }
    return 0; }
