// n4_margin.cpp -- T2 is off by exactly one LSB at every failing angle. T2 = sm(arr(J,S)+K, u2)
// discards u2 low bits.  If the failures are NEAR-TIE cases -- P sitting just beside a truncation
// boundary, so a tiny upstream error in J flips the result -- that explains why only 294 of 65536 fail.
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
static Fsca F;
#define ARR fsca2_arr
#define SM  fsca2_sm
int main(){ if(!load_fsca(F)) return 1;
    // margin = how far P is from the nearest truncation boundary, expressed in J UNITS
    // (one unit of J moves P by about S, so margin_in_J = margin_in_P / S)
    long long nb[12], ng[12]; memset(nb,0,sizeof nb); memset(ng,0,sizeof ng);
    int nf=0, np=0; double sb=0, sg=0;
    for(int a=0;a<65536;a++){
        int q=(int)((a+512u)>>10), m=q&63, t=a-1024*q, flip=0; if(t<-512)t+=65536;
        if(m>=33){m-=32;} if(m>=17){m=32-m;flip=1;} (void)flip;
        if(!FSCA2_S[m]) continue;
        int64_t dx=(int64_t)t<<13;
        int64_t I1=(ARR(dx,FSCA2_KC)+FSCA2_K)>>23, J=(ARR(I1,dx)+FSCA2_K)>>20;
        int64_t P=ARR(J,FSCA2_S[m])+FSCA2_K; int u=FSCA2_U2[m];
        int64_t Q = P>=0? P : ~P;                       // sm() truncates Q
        int64_t rem = Q & ((1LL<<u)-1);
        int64_t dist = std::min<int64_t>(rem+1, (1LL<<u)-rem);    // distance to a boundary, in P units
        double mj = (double)dist / (double)FSCA2_S[m];   // in J units
        bool bad = (F.srn[a]!=fsca2_sin(a,0)) || (F.srz[a]!=fsca2_sin(a,1));
        int b = mj<0.25?0 : mj<0.5?1 : mj<1?2 : mj<2?3 : mj<4?4 : mj<8?5 : mj<16?6 : mj<32?7 : mj<64?8 : mj<128?9 : mj<256?10 : 11;
        if(bad){ nb[b]++; nf++; sb+=mj; } else { ng[b]++; np++; sg+=mj; }
    }
    const char* lab[12]={"<0.25","0.25-0.5",".5-1","1-2","2-4","4-8","8-16","16-32","32-64","64-128","128-256",">=256"};
    printf("distance from the final truncation boundary, expressed in J units\n");
    printf("(one unit of J moves arr(J,S) by S, so this says how big an upstream J error it would take\n");
    printf(" to flip T2 by one LSB)\n\n");
    printf("  %-10s %10s %10s %10s\n","margin","FAILING","passing","fail rate");
    for(int b=0;b<12;b++){ long long tot=nb[b]+ng[b]; if(!tot) continue;
        printf("  %-10s %10lld %10lld %9.3f%%\n",lab[b],nb[b],ng[b],100.0*nb[b]/tot); }
    printf("\n  failing angles: %d   mean margin %.2f J units\n",nf,sb/(nf?nf:1));
    printf("  passing angles: %d   mean margin %.2f J units\n",np,sg/(np?np:1));
    return 0; }
