// fv_certificate_c.cpp -- the rounding constant EXHAUSTED, independently.
// For each (role,g,s2): which K2 = q*2^s2 + j*2^16 make EVERY offset admit some integer I1?
// Same dataset build and same provable-bracket scan as fv_certificate_b.cpp (self-contained copy).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0PUB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2PUB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 JL[1024],JH[1024];
static const i64 EB=1ll<<21, SDEV=1ll<<21;
static void build_ds(int DU){ i64 LIM=12000000ll<<DU;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0PUB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2PUB[n]+DU; i64 S=TB[n].S;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; } }
// 1 = some I1 exists, 0 = provably none.  skip=1 allows the "window wider than one step" shortcut.
static int certify(int t,int g,int s2,i64 K2,int role,i64 jl,i64 jh,int skip,i64*mini1){
    i64 dx=(i64)t<<g; if(jl>jh) return 0;
    if(dx==0){ for(i64 v=-4;v<=4;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=(P+K2)>>s2;
                 if(J>=jl&&J<=jh){ if(mini1)*mini1=v; return 1; } } return 0; }
    i64 Plo=jl*(1ll<<s2)-K2, Phi=(jh+1)*(1ll<<s2)-1-K2; i64 ad=dx<0?-dx:dx;
    if(skip && Phi-Plo+1 >= ad+SDEV+1) return 1;
    i64 lo=Plo-EB, hi=Phi+EB, a,b;
    if(dx>0){ a=(lo>=0? lo/dx : -((-lo+dx-1)/dx)); b=(hi>=0? hi/dx : -((-hi+dx-1)/dx)); }
    else    { a=(hi>=0? -(hi/ad) : (-hi+ad-1)/ad); b=(lo>=0? -(lo/ad) : (-lo+ad-1)/ad); }
    a-=2;b+=2; if(a<-(1ll<<27))a=-(1ll<<27); if(b>(1ll<<27))b=(1ll<<27);
    i64 best=-1;
    for(i64 v=a;v<=b;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=(P+K2)>>s2;
        if(J>=jl&&J<=jh){ if(!mini1) return 1; i64 av=v<0?-v:v; if(best<0||av<best)best=av; } }
    if(best>=0){ if(mini1)*mini1=best; return 1; } return 0; }
int main(int argc,char**argv){ if(!load_fsca(F)){puts("cannot load dump");return 1;} build_tables(); build_ds(2);
    int g0=argc>1?atoi(argv[1]):13, role0=argc>2?atoi(argv[2]):0;
    printf("=== K2 exhausted, fine scale, g=%d, role arr(%s) ===\n",g0,role0?"dx,I1":"I1,dx");
    for(int s2=g0+1;s2<=g0+9;s2++){
        long long tot_ok=0; int best_q=-99; long long best_cnt=0; std::vector<i64> winners;
        int NJ=(s2>16)?(1<<(s2-16)):1;
        for(int q=-4;q<=8;q++){ long long cnt=0; i64 firstK=-1; std::vector<char> ok(NJ,0);
            #pragma omp parallel for schedule(dynamic)
            for(int j=0;j<NJ;j++){ i64 K2=(i64)q*(1ll<<s2)+(i64)j*65536; int feas=0;
                for(int t=-512;t<=511;t++) feas+=certify(t,g0,s2,K2,role0,JL[t+512],JH[t+512],1,0);
                ok[j]=(feas==1024); }
            for(int j=0;j<NJ;j++) if(ok[j]){ i64 K2=(i64)q*(1ll<<s2)+(i64)j*65536; cnt+=65536; if(firstK<0)firstK=K2; winners.push_back(K2); }
            tot_ok+=cnt; if(cnt>best_cnt){best_cnt=cnt;best_q=q;}
            if(cnt) printf("  s2=%2d q=%+d : %lld of %lld K2 values admit ALL 1024 offsets (first K2=%lld)\n",
                           s2,q,cnt,(long long)(1ll<<s2),(long long)firstK); }
        if(!tot_ok){ printf("  s2=%2d : EMPTY for every rounding constant (q in [-4,8])\n",s2); continue; }
        // max over t of the smallest |I1| for the best K2
        i64 K2=winners[0]; i64 mx=0; int argt=0;
        for(int t=-512;t<=511;t++){ i64 mi=-1; certify(t,g0,s2,K2,role0,JL[t+512],JH[t+512],0,&mi);
            if(mi>mx){mx=mi;argt=t;} }
        fflush(stdout); printf("  s2=%2d : best q=%+d (%lld values) ; with K2=%lld the largest smallest-|I1| over t is %lld (at t=%d)\n",
               s2,best_q,best_cnt,(long long)K2,(long long)mx,argt); }
    // and what s2<=g+5 can do at all: how many offsets does the BEST K2 reach?
    puts("\n  best achievable feasible-offset count per s2 (over all q,j):");
    for(int s2=g0-2;s2<=g0+9;s2++){ int best=0; i64 bk=0; int NJ=(s2>16)?(1<<(s2-16)):1;
        std::vector<int> fb(13*NJ,0);
        #pragma omp parallel for schedule(dynamic)
        for(int idx=0;idx<13*NJ;idx++){ int q=idx/NJ-4, j=idx%NJ; i64 K2=(i64)q*(1ll<<s2)+(i64)j*65536; int feas=0;
            for(int t=-512;t<=511;t++) feas+=certify(t,g0,s2,K2,role0,JL[t+512],JH[t+512],1,0); fb[idx]=feas; }
        for(int idx=0;idx<13*NJ;idx++) if(fb[idx]>best){ best=fb[idx]; bk=(i64)(idx/NJ-4)*(1ll<<s2)+(i64)(idx%NJ)*65536; }
        int p17=0,p342=0,pm342=0;
        p17=certify(17,g0,s2,bk,role0,JL[17+512],JH[17+512],1,0);
        p342=certify(342,g0,s2,bk,role0,JL[342+512],JH[342+512],1,0);
        pm342=certify(-342,g0,s2,bk,role0,JL[-342+512],JH[-342+512],1,0);
        printf("    s2=%2d (s2-g=%2d): best %4d/1024 at K2=%lld   [t=17 %d, t=342 %d, t=-342 %d]\n",
               s2,s2-g0,best,(long long)bk,p17,p342,pm342); }
    return 0; }
