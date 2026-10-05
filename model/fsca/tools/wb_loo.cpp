// wb_loo.cpp -- leave-one-node-out: how much of the J-window tightness comes from each node instance?
// For each node m in 1..31 recompute the per-t intersection without m and report the chain's score.
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 NL[32][1024], NH[32][1024];   // per-node allowed J interval
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 base=C0FIT[n]+s*T1-s*T3; i64 t2lo=base-hi,t2hi=base-lo; int u=U2FIT[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            NL[node][t+512]=a1; NH[node][t+512]=a2; } }
    auto score=[&](int skip1,int skip2){ int sc=0,infeas=0,disj=0;
        std::vector<i64> JL(1024),JH(1024); std::vector<char> ok(1024,0);
        for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
            for(int node=1;node<=31;node++){ if(node==skip1||node==skip2)continue;
                if(NL[node][t+512]>gl)gl=NL[node][t+512]; if(NH[node][t+512]<gh)gh=NH[node][t+512]; }
            JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; if(gl>gh)infeas++; }
        for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 dx=(i64)t<<13;
            i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20;
            if(J>=JL[t+512]&&J<=JH[t+512])sc++; }
        for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!ok[ip]||!ok[im])continue;
            if(JH[ip]<JL[im]||JH[im]<JL[ip])disj++; }
        return std::make_tuple(sc,infeas,disj); };
    auto [s0,i0,d0]=score(-1,-1);
    printf("all nodes: chain score %d, infeasible %d, disjoint %d\n",s0,i0,d0);
    puts("skip  score  infeas  disj   (table entry)");
    for(int m=1;m<=31;m++){ auto [s,i,d]=score(m,-1);
        printf("%3d  %5d   %4d   %4d    tab %d\n",m,s,i,d,m<17?m:32-m); }
    // also skip both instances of a table entry
    puts("skip pair (n and 32-n):");
    for(int n=1;n<=15;n++){ auto [s,i,d]=score(n,32-n);
        printf("tab %2d  %5d   %4d   %4d\n",n,s,i,d); }
    return 0; }
