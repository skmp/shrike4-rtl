// n3_stage.cpp -- back-propagate the hardware window to EVERY stage, for EVERY failing angle.
// Answers: at which stage does the model first leave the allowed interval, and by how much?
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
static Fsca F;
#define ARR fsca2_arr
#define SM  fsca2_sm
struct R { int a,t,m,neg; int64_t dI,dJ,dT2,dV; bool okI,okJ; };
int main(){ if(!load_fsca(F)) return 1;
    std::map<int64_t,int> hI,hJ,hT2,hV; int nfail=0,noI=0,noJ=0; std::vector<R> rs;
    for(int a=0;a<65536;a++){
        uint32_t hrn=F.srn[a],hrz=F.srz[a];
        if(hrn==fsca2_sin(a,0)&&hrz==fsca2_sin(a,1)) continue;
        nfail++;
        int q=(int)((a+512u)>>10), m=q&63, t=a-1024*q, neg=0, flip=0; if(t<-512)t+=65536;
        if(m>=33){m-=32;neg=1;} if(m>=17){m=32-m;flip=1;}
        int64_t s=flip?-1:1, dx=(int64_t)t<<13;
        int64_t T1=FSCA2_C1F[m]?SM(ARR(FSCA2_C1F[m],dx)+FSCA2_K,23-FSCA2_E1[m]):0;
        int64_t A3=((ARR(((int64_t)t*t)<<3,dx)+FSCA2_K)>>20)>>3;
        int64_t T3=FSCA2_C3[m]?SM(ARR(FSCA2_C3[m]<<5,A3)+FSCA2_K,26):0;
        int64_t I1=(ARR(dx,FSCA2_KC)+FSCA2_K)>>23, J=(ARR(I1,dx)+FSCA2_K)>>20;
        int64_t T2=FSCA2_S[m]?SM(ARR(J,FSCA2_S[m])+FSCA2_K,FSCA2_U2[m]):0;
        int64_t V=FSCA2_C0[m]+s*T1-T2-s*T3;
        Win w=decode(hrz,hrn); int64_t lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        int64_t Vlo=neg?-hi:lo, Vhi=neg?-lo:hi;
        int64_t dV = V<Vlo? V-Vlo : (V>Vhi? V-Vhi : 0);
        int64_t T2lo=FSCA2_C0[m]+s*T1-s*T3-Vhi, T2hi=FSCA2_C0[m]+s*T1-s*T3-Vlo;
        int64_t dT2 = T2<T2lo? T2-T2lo : (T2>T2hi? T2-T2hi : 0);
        int64_t jl=0,jh=0; bool gj=false;
        for(int64_t c=J-40000;c<=J+40000;c++){ int64_t tc=FSCA2_S[m]?SM(ARR(c,FSCA2_S[m])+FSCA2_K,FSCA2_U2[m]):0;
            if(tc>=T2lo&&tc<=T2hi){ if(!gj){jl=c;gj=true;} jh=c; } }
        int64_t dJ = !gj?0:(J<jl? J-jl : (J>jh? J-jh : 0));
        int64_t il=0,ih=0; bool gi=false;
        for(int64_t c=I1-6000;c<=I1+6000;c++){ int64_t jj=(ARR(c,dx)+FSCA2_K)>>20;
            int64_t tc=FSCA2_S[m]?SM(ARR(jj,FSCA2_S[m])+FSCA2_K,FSCA2_U2[m]):0;
            if(tc>=T2lo&&tc<=T2hi){ if(!gi){il=c;gi=true;} ih=c; } }
        int64_t dI = !gi?0:(I1<il? I1-il : (I1>ih? I1-ih : 0));
        if(!gj) noJ++; if(!gi) noI++;
        hV[dV]++; hT2[dT2]++; if(gj)hJ[dJ]++; if(gi)hI[dI]++;
        rs.push_back({a,t,m,neg,dI,dJ,dT2,dV,gi,gj});
    }
    printf("failing angles: %d\n",nfail);
    printf("  no feasible J at all within +-40000 : %d\n",noJ);
    printf("  no feasible I1 at all within +-6000 : %d\n\n",noI);
    auto show=[&](const char*n,std::map<int64_t,int>&h){ printf("  %-26s",n); int tot=0;
        for(auto&p:h){ printf(" %+lld:%d ",(long long)p.first,p.second); tot+=p.second; } printf("  (n=%d)\n",tot); };
    printf("how far OUTSIDE its allowed interval is the model, per stage:\n");
    show("V   (final, 2^-26)",hV); show("T2  (square term)",hT2);
    show("J   (coarse latch)",hJ); show("I1  (FIRST latch)",hI);
    int i0=0,j0=0; for(auto&r:rs){ if(r.okI&&r.dI==0)i0++; if(r.okJ&&r.dJ==0)j0++; }
    printf("\n  angles where I1 is ALREADY inside its allowed interval: %d of %d\n",i0,nfail);
    printf("  angles where J  is ALREADY inside its allowed interval: %d of %d\n",j0,nfail);
    printf("  -> where I1 is already fine but the result is wrong, the error is introduced AFTER I1.\n");
    printf("\nfirst 12 failing angles, per-stage offsets:\n");
    printf("   angle   t     node  dI1   dJ    dT2   dV\n");
    for(int i=0;i<12&&i<(int)rs.size();i++){ R&r=rs[i];
        printf("  0x%04X %+5d  %3d %s %+5lld %+5lld %+5lld %+5lld\n",r.a,r.t,r.m,r.neg?"-":"+",
            (long long)r.dI,(long long)r.dJ,(long long)r.dT2,(long long)r.dV); }
    return 0; }
