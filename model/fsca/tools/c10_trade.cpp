// c10_trade.cpp -- the resolution/asymmetry trade-off, quantified at both pinned extremes.
// Resolution: at t=17 the achievable J values must be dense enough to hit 45617 exactly.
// Asymmetry : at |t|=342 the sign asymmetry must reach 22..31.
// For the CHAIN (constant x dx -> latch L -> x dx), the second operand dx is EXACT, so the granularity of J is
// dx/2^s2 = t/2^(s2-13) -- fine at small t -- while the asymmetry is one L-LSB, also t/2^(s2-13).  The two are
// the SAME expression, so the chain must satisfy  17/2^(s2-13) <= 1  and  342/2^(s2-13) >= 21  simultaneously.
// For the TWO-LADDER form the second operand is itself a small latch at small t, so the granularity blows up.
#include "base2.h"
#include <cstdio>
#include <set>
#include <algorithm>
static i64 JL[1024],JH[1024];
static bool load(const char*p){ FILE*f=fopen(p,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){JL[t+512]=a;JH[t+512]=b;} } fclose(f); return true; }
int main(){ build_tables(); load("jwin18.txt");
    puts("CHAIN:  L = (arr(C,dx)+K1)>>s1 ; J = (arr(L,dx)+K2)>>s2 ,  s1+s2 = 41");
    puts("  s2 | J granularity at t=17 | asymmetry at |t|=342 | both OK?   | J(17) values reachable / target hit");
    for(int s2=14;s2<=22;s2++){ int s1=41-s2;
        double gran=17.0/(double)(1ll<<(s2-13)), asym=342.0/(double)(1ll<<(s2-13));
        std::set<i64> v17; int hit=0;
        for(i64 dk=-400;dk<=400;dk++){ i64 C=KC+dk;
          for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++){
            i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0;
            i64 dx=(i64)17<<13; i64 L=(arr(C,dx)+K1)>>s1; i64 J=(arr(L,dx)+K2)>>s2;
            v17.insert(J); if(J==JL[17+512]) hit++; } }
        printf("  %2d |        %8.3f       |      %8.2f        |  %-9s | %3zu distinct, target hit %d\n",
            s2,gran,asym,(gran<=1.05&&asym>=21.0)?"BOTH":(gran<=1.05?"res only":(asym>=21.0?"asym only":"neither")),
            v17.size(),hit); }
    puts("\nTWO-LADDER: L1,L2 both latches proportional to t -> at t=17 both factors are small");
    puts("  s3 | distinct J(17) over the whole admissible band | target reachable?");
    for(int s3=9;s3<=13;s3++){ std::set<i64> v; int hit=0; int s1=24;
        for(int s2=16;s2<=28;s2++){ int m=s1+s2+s3-41; if(m<0||m>46) continue; i64 sh1=1ll<<(s1-13);
            for(i64 C1=(i64)(8.0*sh1); C1<=(i64)(300.0*sh1); C1+=1){
                __int128 num=(__int128)KC<<m; i64 C2=(i64)(num/C1); if(C2<(1ll<<17)||C2>=(1ll<<26)) continue;
                for(i64 d=-2;d<=2;d++) for(int k=0;k<8;k++){
                    i64 K1=(k&1)?(1ll<<19):0,K2=(k&2)?(1ll<<19):0,K3=(k&4)?(1ll<<19):0;
                    i64 dx=(i64)17<<13; i64 L1=(arr(C1,dx)+K1)>>s1, L2=(arr(C2+d,dx)+K2)>>s2;
                    i64 J=(arr(L1,L2)+K3)>>s3; v.insert(J); if(J==JL[17+512]) hit++; } } }
        printf("  %2d | %6zu%s | %s\n",s3,v.size(),"                                        ",hit?"YES":"no"); }
    return 0; }
