// wv_feedback_nat.cpp -- is the claimed A a derived constant, and does any natural-constant member
// of the claimed chain family keep the guard nodes exact?
#include "base2.h"
#include <cstdio>
#include <vector>
#include <cstring>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} ex=w.exact; }
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static i64 JL[1024],JH[1024]; static int OKT[1024];
static void load_win(){ FILE*f=fopen("jwin.txt","r"); int t,ok; long long l,h;
    while(fscanf(f,"%d %lld %lld %d",&t,&l,&h,&ok)==4){JL[t+512]=l;JH[t+512]=h;OKT[t+512]=ok;} fclose(f); }
static inline i64 Vof(int n,int t,int flip,i64 J){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 T2=T.S? sm(arr(J,T.S)+K,U2T[n]):0; i64 s=flip?-1:1; return C0T[n]+s*T1-T2-s*T3; }
int main(){ if(!load_fsca(F))return 1; build_tables(); load_win();
    printf("KC=%lld  2*KC+1=%lld  (is 2KC+1 < 2^24? %s)\n",(long long)KC,(long long)(2*KC+1),(2*KC+1<(1ll<<24))?"yes, a 24-bit operand":"no");
    printf("claim A = 2*(2KC+1)? %lld vs 20694294 : %s\n",(long long)(2*(2*KC+1)),2*(2*KC+1)==20694294?"EXACT":"no");
    printf("claim B = 4*(2KC+1)? %lld vs 41388588 : %s\n",(long long)(4*(2*KC+1)),4*(2*KC+1)==41388588?"EXACT":"no");
    printf("claim C = 8*(2KC+1)? %lld vs 82777175 : %s (off by %lld)\n",(long long)(8*(2*KC+1)),
        8*(2*KC+1)==82777175?"EXACT":"no",(long long)(82777175-8*(2*KC+1)));
    // long double scale check only (not decisive): what does the Chebyshev-economised 2pi^2 give?
    { long double pi=3.14159265358979323846264338327950288L; long double ec=2*pi*pi*(1.0L-(4*pi*pi)/12.0L/16384.0L);
      printf("scale check (long double only): economised 2pi^2 = %.9Lf ; *2^18 = %.1Lf (KC=%lld) ; *2^19 = %.1Lf (2KC+1=%lld)\n",
        ec, ec*262144.0L,(long long)KC, ec*524288.0L,(long long)(2*KC+1)); }
    puts("\n   A            K1         K2   s1  Jscore/1022   e2e   n0 n1 n2 n16  guards");
    std::vector<i64> AS={4*KC, 2*(2*KC+1)};
    std::vector<i64> K1S={0, 1ll<<19, -(1ll<<19), 1ll<<24, -(1ll<<24), -3276800};
    std::vector<i64> K2S={0, 1ll<<19};
    for(i64 A:AS) for(i64 K1:K1S) for(i64 K2:K2S){
        int sc=0; for(int ti=0;ti<1024;ti++){ if(!OKT[ti])continue; int t=ti-512; i64 dx=(i64)t<<13;
            i64 L=(arr(A,dx)+K1)>>25; i64 J=sm(arr(L,dx)+K2,20); if(J>=JL[ti]&&J<=JH[ti])sc++; }
        int tot=0,g[4]={0,0,0,0};
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; int v=0;
            for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 dx=(i64)t<<13; i64 L=(arr(A,dx)+K1)>>25; i64 J=sm(arr(L,dx)+K2,20);
                i64 V=Vof(n,t,flip,J); if(V<lo||V>hi)v++; }
            tot+=v; if(node==0)g[0]=v; if(node==1)g[1]=v; if(node==2)g[2]=v; if(node==16)g[3]=v; }
        printf("%10lld %11lld %8lld  %2d   %5d       %5d  %2d %2d %2d %3d   %s\n",
            (long long)A,(long long)K1,(long long)K2,25,sc,tot,g[0],g[1],g[2],g[3],(g[0]|g[1]|g[2]|g[3])?"BROKEN":"exact"); }
    return 0; }
