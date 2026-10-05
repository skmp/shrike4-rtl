// v10_arr.cpp -- the ONE placement where "a 1s-complement value consumed as 2s complement" is
// SIGN-INDEPENDENT and can therefore move J(17) and J(342): the Booth array's own negated rows.
// base2.h already stores a negated row as ~mag (1s complement) and adds the missing hot one only
// from column 18 up.  Sweep that threshold, plus the two row-sign conventions, and score.
#include "fsca_common.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdio>
typedef __int128 i128;
static int HOTCOL = 18;      // hot one added when 2k >= HOTCOL ; 99 = never, 0 = always
static int XORSIGN = 0;      // 0 = negate by digit sign (base2.h), 1 = negate by XOR of both signs
static i64 arr2(i64 rec, i64 mul){ i64 R[14]; i64 T=0; int msgn = (XORSIGN && mul<0)?1:0;
  for(int k=0;k<14;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
    int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
    int neg = (d<0); if(msgn) neg ^= (d!=0)?1:0;   // XOR variant only flips when the digit is nonzero
    i64 mag=(i64)(d<0?-d:d)*mul; i64 row = neg? ~mag : mag; R[k]=row*(1ll<<(2*k));
    T += (R[k]>>16)<<16; if(neg && 2*k>=HOTCOL) T += 1ll<<(2*k); }
  int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T += (1ll<<16)*(m15>=2);
  int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T -= (1ll<<16)*p16; return T; }
static inline i64 sm2(i64 P,int u){ return P>=0 ? (P>>u) : -((~P)>>u); }
static const i64 KK=1ll<<19, KC2=0x4ef145;
// --- the derived tables, copied from base2.h's build_tables (values only, recomputed here) -----
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static i64 WL[1024],WH[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); char l[512];
  while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)==4){WL[t+512]=a;WH[t+512]=b;} } fclose(f); }
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
int main(){ if(!load_fsca(F))return 1; build_tables(); load_jwin();
  puts("=== v10_arr: the sign-INDEPENDENT complement placement (the Booth row negation) ===");
  puts("FIPR-VALIDITY GATE: a variant is only admissible if it leaves the FIPR-visible result unchanged");
  puts("for both operands in [2^23,2^24), which is the only domain the 32M-vector dump validates.\n");
  printf("%-22s %-8s | FIPR-equiv | jwin18 | full | J17    J342     J-342\n","variant","");
  for(int xs=0;xs<2;xs++) for(int hc : {0,2,4,6,8,10,12,14,16,18,20,22,99}){
    HOTCOL=hc; XORSIGN=xs;
    // FIPR equivalence check on a random-but-fixed sample of in-domain operand pairs
    bool eq=true; uint64_t st=88172645463325252ull;
    for(int i=0;i<20000 && eq;i++){ st^=st<<13; st^=st>>7; st^=st<<17;
      i64 a=(i64)((st>>10)&0x7fffff)+ (1ll<<23); st^=st<<13; st^=st>>7; st^=st<<17;
      i64 b=(i64)((st>>10)&0x7fffff)+ (1ll<<23);
      if(((arr2(a,b)+KK)>>20) != ((arr(a,b)+KK)>>20)) eq=false; }
    int sc=0; i64 J17=0,J342=0,Jm342=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr2(KC2,dx)+KK)>>23; i64 Jf=(arr2(I1,dx)+KK)>>18;
      if(t==17)J17=Jf; if(t==342)J342=Jf; if(t==-342)Jm342=Jf;
      if(Jf>=WL[t+512]&&Jf<=WH[t+512])sc++; }
    int viol=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 T1=TB[n].C1f? sm2(arr2(TB[n].C1f,dx)+KK,23-TB[n].e1):0;
        i64 A3=((arr2(((i64)t*t)<<3,dx)+KK)>>20)>>3; i64 T3=TB[n].c3? sm2(arr2(TB[n].c3<<5,A3)+KK,26):0;
        i64 I1=(arr2(KC2,dx)+KK)>>23, J=(arr2(I1,dx)+KK)>>20;
        i64 T2=TB[n].S? sm2(arr2(J,TB[n].S)+KK,U2TAB[n]):0;
        i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi); if(V<lo||V>hi)viol++; } }
    char hb[16]; if(hc==99) snprintf(hb,sizeof hb,"never"); else snprintf(hb,sizeof hb,"%d",hc); char nm[64]; snprintf(nm,sizeof nm,"%s hot@2k>=%s",xs?"XORsign":"digitsign",hb);
    printf("%-31s | %-10s | %6d | %4d | %lld %lld %lld%s\n",nm,eq?"YES":"no",sc,viol,
      (long long)J17,(long long)J342,(long long)Jm342,
      (J17==45617)?"   *** J(17) HIT ***":"");
  }
  puts("\ntargets: J17=45617  J342=18466836  J-342 in [18466858,18466867]   baseline: digitsign hot@2k>=18");
  return 0; }
