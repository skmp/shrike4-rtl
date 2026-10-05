// wb_t342.cpp -- t = +-342 is the single obstruction: it is unreachable by L x dx at every shift, and it alone
// forces the asymmetry rate above every rung.  Which node constraints pin it, and what happens if one is dropped?
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int TT=argc>1?atoi(argv[1]):342;
    for(int sgn=0;sgn<2;sgn++){ int t=sgn?-TT:TT; i64 dx=(i64)t<<13;
        printf("\n=== t=%d   FINE latch (u2 = 32+k, J ~ 157.88 t^2) ===\n",t);
        printf("node tab  exact?  Jlo        Jhi      width\n");
        i64 gl=0,gh=48000000; int blo=-1,bhi=-1;
        i64 NLo[32],NHi[32];
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 base=C0FIT[n]+s*T1-s*T3; i64 t2lo=base-hi,t2hi=base-lo; int u=U2FIT[n]+2;
            i64 a1=0,b1=48000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=48000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            NLo[node]=a1;NHi[node]=a2;
            if(a1>gl){gl=a1;blo=node;} if(a2<gh){gh=a2;bhi=node;}
            if(a2-a1<800) printf("%3d %3d   %d   %9lld %9lld %6lld\n",node,n,(int)ex,(long long)a1,(long long)a2,(long long)(a2-a1)); }
        printf("intersection [%lld,%lld] width %lld ; lower bound from node %d, upper from node %d\n",
            (long long)gl,(long long)gh,(long long)(gh-gl),blo,bhi);
        // leave-one-out
        printf("leave-one-out widths: ");
        for(int m=1;m<=31;m++){ i64 l=0,h=48000000;
            for(int node=1;node<=31;node++){ if(node==m||!TB[node>=17?32-node:node].S)continue;
                if(NLo[node]>l)l=NLo[node]; if(NHi[node]<h)h=NHi[node]; }
            if(h-l>gh-gl) printf("drop%d:[%lld,%lld] ",m,(long long)l,(long long)h); }
        printf("\n"); }
    return 0; }
