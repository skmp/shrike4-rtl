// fv_final-fields_5_misc.cpp -- INDEPENDENT checks of the remaining sub-claims:
//  (1) the Booth recoder's validity limit (a recoded operand must lie in [-2^27, 2^27))
//  (2) RN22 vs RN23: are the J windows a strict loosening?
//  (3) "the free-J floor is 0 iff all 1024 offsets are feasible" -- tautology or measurement?
//  (4) "the fixed-point field's leading-zero count equals the stored octave k" over 448 cases
//  (5) the 23-bit normalised sine field vs the top 23 bits of the normalised 26-bit C0 ROM
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
static inline i64 rmode(i128 num,i128 den,int mode){ i128 q=num/den,r=num%den;
    switch(mode){ case 0: if(r*2>den||(r*2==den&&(q&1)))++q; break; case 1: break;
                  case 2: if(r*2>=den)++q; break; default: if(r>0)++q; } return (i64)q; }
// build the fine-scale latch dataset for a given normalised field width W (u = W+7+k, +DU)
static int build(int W,int rnd,int DU,std::vector<i64>&L,std::vector<i64>&H){
    i64 S[17]; int u[17];
    for(int n=0;n<=16;n++){ int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
        S[n]= n? std::min(rmode(s<<(W+k),QONE,rnd),(i64)((1ll<<W)-1)) : 0; u[n]=W+7+k+DU; }
    const i64 LIM=12000000ll<<DU; int feas=0; L.assign(1024,0); H.assign(1024,0);
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13,gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
            i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(((arr(m,S[n])+K)>>u[n])>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(((arr(m,S[n])+K)>>u[n])<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        L[t+512]=gl; H[t+512]=gh; if(gl<=gh)feas++; }
    return feas; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    puts("== 1. Booth recoder validity range of arr(rec, mul) ==");
    { i64 mul=342*8192; i64 bad=0,first=0;
      for(i64 x=(1ll<<27)-4096; x<(1ll<<27)+4096; x++){ if(arr(x,mul)<arr(x-1,mul)){ bad++; if(!first)first=x; } }
      printf("   ascending rec near 2^27 with mul=%lld: %lld monotonicity breaks, first at rec = %lld  (2^27 = %lld)\n",
             (long long)mul,(long long)bad,(long long)first,(long long)(1ll<<27));
      printf("   exactness: arr(2^27-1,1)=%lld (want %lld)   arr(2^27,1)=%lld (WRAPS to %lld)\n",
             (long long)arr((1ll<<27)-1,1),(long long)((1ll<<27)-1),(long long)arr(1ll<<27,1),(long long)arr(1ll<<27,1));
      i64 b2=0,f2=0; for(i64 x=(1ll<<29)-4096; x<(1ll<<29)+4096; x++){ if(arr(12345,x)<arr(12345,x-1)){b2++; if(!f2)f2=x;} }
      printf("   as MULTIPLICAND (arr(12345,x)) near 2^29: %lld breaks, first at %lld\n",(long long)b2,(long long)f2); }
    puts("\n== 2. RN22 (u=29+k) vs RN23 (u=30+k): same dT2/dJ, so the windows are comparable ==");
    { std::vector<i64> L23,H23,L22,H22; int f23=build(23,0,2,L23,H23), f22=build(22,0,2,L22,H22);
      long s23=0,s22=0; int w1_23=0,w1_22=0,w4_23=0,w4_22=0,superset=0,tighter=0,ndiff=0;
      for(int i=0;i<1024;i++){ i64 a=H23[i]-L23[i]+1,b=H22[i]-L22[i]+1; s23+=a; s22+=b;
        if(a==1)w1_23++; if(b==1)w1_22++; if(a<=4)w4_23++; if(b<=4)w4_22++;
        if(L22[i]<=L23[i]&&H22[i]>=H23[i])superset++; if(b<a)tighter++; if(a!=b)ndiff++; }
      printf("   feasible: RN23 %d/1024, RN22 %d/1024\n",f23,f22);
      printf("   mean window: RN23 %.2f   RN22 %.2f\n",(double)s23/1024,(double)s22/1024);
      printf("   #width==1: RN23 %d -> RN22 %d ;  #width<=4: RN23 %d -> RN22 %d\n",w1_23,w1_22,w4_23,w4_22);
      printf("   RN22 window contains the RN23 window at %d/1024 offsets; RN22 strictly TIGHTER at %d offsets\n",superset,tighter);
      printf("   t=342: RN23 [%lld,%lld] width %lld ; RN22 [%lld,%lld] width %lld\n",
             (long long)L23[854],(long long)H23[854],(long long)(H23[854]-L23[854]+1),
             (long long)L22[854],(long long)H22[854],(long long)(H22[854]-L22[854]+1));
      // what J does the RN22 chain reach at (s2=18,K2=0) for t=342?
      { i64 dx=342*8192,best=-1; for(i64 I1=0;I1<(1ll<<27);I1++){ i64 J=(arr(I1,dx))>>18;
            if(J>=L22[854]&&J<=H22[854]){best=J;break;} if(J>H22[854])break; }
        printf("   RN22 at (s2=18,K2=0), t=342: first reachable J in window = %lld (RN23 pinned value 18466836)\n",(long long)best); } }
    puts("\n== 3. is the 'free-J floor' independent of the feasible count? ==");
    { std::vector<i64> L,H; int feas=build(23,0,2,L,H);
      int infeas=0; for(int i=0;i<1024;i++) if(L[i]>H[i])infeas++;
      printf("   by construction the per-offset minimum violation count is 0 at every FEASIBLE offset\n");
      printf("   (feasible == some J reproduces all 31 node instances == 0 violations at that t)\n");
      printf("   and >= 1 at every INFEASIBLE offset.  Hence floor == 0 <=> feasible == 1024.\n");
      printf("   measured for W=23 RNe: feasible %d, infeasible %d, therefore floor = %s\n",
             feas,infeas,infeas?"> 0":"0 (exactly, with no further computation)"); }
    puts("\n== 4. does the fixed-point field's leading-zero count equal the stored octave k? ==");
    { int tested=0,bad=0;
      for(int W=20;W<=26;W++) for(int rnd=0;rnd<4;rnd++) for(int n=1;n<=16;n++){
          i128 s=sin_(PI_()*n/32); i64 f=rmode(s<<W,QONE,rnd); if(f<=0){continue;}
          int lz = W-1-(63-__builtin_clzll((u64)f));      // leading zeros inside a W-bit field
          if(lz!=TB[n].u2-30)bad++; tested++; }
      printf("   %d (W,rounding,node) cases tested, %d mismatches -> the two alignment stories %s\n",
             tested,bad,bad?"DIFFER":"are arithmetically identical here"); }
    puts("\n== 5. 23-bit normalised sine field vs the top 23 bits of the NORMALISED 26-bit C0 ROM ==");
    { const char*RN[4]={"RNe","trunc","half-up","ceil"};
      for(int rnd=0;rnd<4;rnd++){ int diff=0,maxd=0;
        for(int n=1;n<=16;n++){ int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
          i64 a=std::min(rmode(s<<(23+k),QONE,rnd),(i64)((1ll<<23)-1));
          i64 b=std::min(rmode((i128)C0TAB[n]<<k,(i128)1<<3,rnd),(i64)((1ll<<23)-1));
          if(a!=b){diff++; int d=(int)llabs(a-b); if(d>maxd)maxd=d;} }
        printf("   %-8s: differ at %d of 16 nodes, max |delta| = %d\n",RN[rnd],diff,maxd); } }
    return 0; }
