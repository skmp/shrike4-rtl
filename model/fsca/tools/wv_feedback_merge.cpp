// wv_feedback_merge.cpp -- independent check of the claimed REFUTATION of the merged (fraction-feedback) even part.
// (a) the algebraic identity, verified exhaustively over integers
// (b) the t=0 prediction  E = field << octave  against the exact t=0 windows
// (c) the merged even part end to end: window regeneration + rule scores, scanned over Kr
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} ex=w.exact; }
// my own variable-digit FIPR array (same structure as base2.h's arr, digit count parameterised)
static i64 arrN(i64 rec,i64 mul,int nd){ std::vector<i64> R(nd); i64 T=0;
    for(int k=0;k<nd;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag;
        R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16; if(d<0&&2*k>=18) T+=1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2);
    int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16; return T; }
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static const int W=33;
static i64 J_base(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
static i64 J_rat(int t){ return (KC*(i64)t*(i64)t)>>17; }
static i64 Emerged(i64 C0,i64 J,i64 Kr,int role){ i64 f=(1ll<<W)-J;
    return role==0 ? ((arrN(C0,f,14)+Kr)>>W) : ((arrN(f,C0,18)+Kr)>>W); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    // ---------- (a) exact algebra ----------
    { long bad=0; for(i64 C=1;C<=200000;C+=997) for(i64 J=0;J<=12000000;J+=7919){
        i64 Kr=(1ll<<W)-1-K; i64 lhs=(((1ll<<W)-J)*C+Kr)>>W; i64 rhs=C-((J*C+K)>>W); if(lhs!=rhs)bad++; }
      printf("(a) merged==separate at Kr=2^33-1-2^19, exact products: %s (%ld mismatches)\n", bad?"NO":"YES",bad);
      long bad2=0; for(i64 C=1;C<=200000;C+=997) for(i64 J=0;J<=12000000;J+=7919){
        i64 Kr=1ll<<19; i64 lhs=(((1ll<<W)-J)*C+Kr)>>W; i64 rhs=C-((J*C+K)>>W); if(lhs!=rhs)bad2++; }
      printf("    with Kr=2^19 instead: %ld mismatches of 202*1516 -> the two forms differ\n",bad2); }
    // ---------- (b) t=0 windows vs the shifted sine field ----------
    puts("\n(b) t=0 windows (intersection of nodes n and 32-n) vs predictions");
    puts("  n  win_lo     win_hi    w  S(23b)<<oct  d    RN(sin*2^26) d   fittedC0  d");
    int failS=0,zerowidth=0;
    for(int n=0;n<=16;n++){ i64 lo,hi;bool ex; win_of(n*1024,lo,hi,ex);
        if(n!=0&&n!=16){ i64 l2,h2;bool e2; win_of((32-n)*1024,l2,h2,e2); lo=std::max(lo,l2); hi=std::min(hi,h2); }
        int k=U2T[n]-30; i64 pred = (n==0)?0:(TB[n].S<<(3-k));
        if(hi==lo) zerowidth++;
        int f = (pred<lo||pred>hi); failS+=f;
        printf("%3d %9lld %9lld %2lld  %10lld %+4lld  %10lld %+3lld  %9lld %+3lld %s\n",n,(long long)lo,(long long)hi,
            (long long)(hi-lo),(long long)pred,(long long)(pred-lo),(long long)TB[n].sin26,(long long)(TB[n].sin26-lo),
            (long long)C0T[n],(long long)(C0T[n]-lo), f?"<-- S-shift FAILS":""); }
    printf("  23-bit field<<octave misses the t=0 window at %d of 17 entries; zero-width windows: %d\n",failS,zerowidth);
    // ---------- (c) merged even part end to end ----------
    puts("\n(c) merged even part: window regeneration per Kr   (role 0 = rec:C0, role 1 = rec:fraction)");
    puts("   role         Kr              infeas  disjoint  chain  floor-rule");
    std::vector<i64> KRS={ (1ll<<W)-1-K, 0, 1ll<<19, 1ll<<32, (1ll<<W)-1, (1ll<<W)-(1ll<<19), 1ll<<16, (1ll<<W)-1-(1ll<<18), (1ll<<W)-1-(1ll<<20) };
    for(int role=0;role<2;role++) for(i64 Kr:KRS){
        static i64 JL[1024],JH[1024]; static int OK[1024];
        #pragma omp parallel for schedule(dynamic)
        for(int ti=0;ti<1024;ti++){ int t=ti-512; i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                i64 off=s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 elo=lo-off, ehi=hi-off;              // required E
                // E is non-increasing in J: find the J interval with elo <= E(J) <= ehi
                i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(Emerged(C0T[n],m,Kr,role)<=ehi)b1=m;else a1=m+1;}
                i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(Emerged(C0T[n],m,Kr,role)>=elo)a2=m;else b2=m-1;}
                if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            JL[ti]=gl;JH[ti]=gh;OK[ti]=gl<=gh; }
        int inf=0,dis=0,sc1=0,sc2=0;
        for(int i=0;i<1024;i++) if(!OK[i])inf++;
        for(int a=1;a<=511;a++){int ip=a+512,im=512-a; if(!OK[ip]||!OK[im])continue; if(JH[im]<JL[ip]||JH[ip]<JL[im])dis++;}
        for(int ti=0;ti<1024;ti++){ if(!OK[ti])continue; i64 j1=J_base(ti-512),j2=J_rat(ti-512);
            if(j1>=JL[ti]&&j1<=JH[ti])sc1++; if(j2>=JL[ti]&&j2<=JH[ti])sc2++; }
        printf("   %d   %14lld        %5d    %5d   %5d   %5d\n",role,(long long)Kr,inf,dis,sc1,sc2); }
    return 0; }
