// w5_uni.cpp -- BRANCH 5: the ONE reading of "2s computed / 1s used" that is NOT sign-dependent.
// If  t = angle - 1024*node  is evaluated as  angle + ~(1024*node)  with the carry-in MISSING, the
// result is t-1 for EVERY angle, not just negative ones.  That form can move the positive branch, so
// it is the only member of the family that could touch J(17) and J(342).  Tested at the t level
// (uniform u) and at the dx level (uniform p), square-path-only and whole-datapath.
#include "w5_model.h"
#include <cstdio>
static i64 JL[1024],JH[1024]; static bool have[1024];
int main(){ if(!load_fsca(W5F))return 1; build_tables();
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f);
    printf("--- uniform t-level offset u (square path, jwin18) ---\n");
    for(int u=-4;u<=4;u++){ int s=0,pos=0; i64 j17=0,j342=0,jm=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)(t+u)<<13;
            i64 I1=(arr(dx,KC)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
            if(t==17)j17=j; if(t==342)j342=j; if(t==-342)jm=j;
            if(j>=JL[i]&&j<=JH[i]){s++; if(t>=0)pos++;} }
        printf("  u=%+d : %4d/1024 (pos %d)  J(17)=%lld J(342)=%lld J(-342)=%lld%s\n",u,s,pos,
            (long long)j17,(long long)j342,(long long)jm,u?"":"   <- baseline"); }
    printf("  targets                                45617   18466836  18466858..67\n");
    printf("\n--- uniform dx-level offset p (square path, jwin18) ---\n");
    for(int p=-8;p<=8;p++){ int s=0; i64 j17=0,j342=0,jm=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=((i64)t<<13)+p;
            i64 I1=(arr(dx,KC)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
            if(t==17)j17=j; if(t==342)j342=j; if(t==-342)jm=j;
            if(j>=JL[i]&&j<=JH[i])s++; }
        printf("  p=%+d : %4d/1024   J(17)=%lld J(342)=%lld J(-342)=%lld%s\n",p,s,
            (long long)j17,(long long)j342,(long long)jm,p?"":"   <- baseline"); }
    printf("\n--- uniform t-level offset u, WHOLE datapath, full model over 65536 angles (baseline 294) ---\n");
    // implemented by shifting the node base: reuse the rho machinery is wrong here, so do it inline
    for(int u=-2;u<=2;u++){
        int v=0;
        for(int a=0;a<65536;a++){ int nr=(a+512)>>10; int node=nr&63; int t=a-1024*nr+u;
            int so=1,m=node; if(m>=32){so=-1;m-=32;} int flip=m>=17,n=flip?32-m:m;
            const Tab&T=TB[n]; i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 I1=(arr(dx,KC)+K)>>23, I2=(arr(I1,dx)+K)>>20;
            i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
            i64 s=flip?-1:1; i64 V=so*(C0TAB[n]+s*T1-T2-s*T3);
            i64 lo,hi; w5_win(a,lo,hi); if(V<lo||V>hi)v++; }
        printf("  u=%+d : %6d violations%s\n",u,v,u?"":"   <- baseline"); }
    return 0; }
