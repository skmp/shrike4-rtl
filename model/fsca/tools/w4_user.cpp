// w4_user.cpp -- the USER's two hypotheses, re-measured independently of m7_rep.cpp and extended to
// PLACEMENTS m7 did not cover (the complement misuse can sit on t, on dx at either multiply, or on the
// feedback latch I1), plus the "extra bit" read as a FIELD WIDTH rather than a value change.
//   H1: t is computed in 2s complement and consumed as 1s complement -> the consumer sees t+1 when t<0
//       (and the converse, t-1, if 1s is computed and 2s consumed).
//   H2: t carries one more bit of precision.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
// generic chain with independent per-placement negative-side offsets
//  da : added to t (t<0 only) before forming dx for the FIRST multiply
//  db : added to t (t<0 only) before forming dx for the SECOND multiply
//  di : added to I1 (I1<0 only) before the second multiply
static inline i64 Jgen(int t,int da,int db,int di){
    i64 d1=(i64)(t + (t<0?da:0))<<13, d2=(i64)(t + (t<0?db:0))<<13;
    i64 I1=(arr(KC,d1)+K)>>23; if(I1<0) I1+=di;
    return (arr(I1,d2)+K)>>18; }
static int score(int da,int db,int di){ int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=Jgen(i-512,da,db,di); if(j>=JL[i]&&j<=JH[i])s++; } return s; }
// field-width variant: dx sign-extended from a W-bit field (a narrower/wider t register)
static inline i64 sext(i64 v,int W){ i64 m=1ll<<(W-1); v&=(1ll<<W)-1; return (v^m)-m; }
static int scoreW(int W1,int W2,i64&j17,i64&j342,i64&jm342){
    auto J=[&](int t)->i64{ i64 dx1=sext((i64)t<<13,W1), dx2=sext((i64)t<<13,W2);
        i64 I1=(arr(KC,dx1)+K)>>23; return (arr(I1,dx2)+K)>>18; };
    int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 v=J(i-512); if(v>=JL[i]&&v<=JH[i])s++; }
    j17=J(17); j342=J(342); jm342=J(-342); return s; }
int main(){ load();
    printf("targets: J(17)=45617 exactly, J(342)=18466836 exactly, J(-342) in [%lld,%lld]; baseline 918/1024\n\n",
        (long long)JL[170],(long long)JH[170]);
    printf("=== H1: 2s-computed / 1s-consumed, at EVERY PLACEMENT (offset applies only when the value is negative) ===\n");
    printf("  da = on t for multiply 1, db = on t for multiply 2, di = on the latch I1\n");
    printf("   da  db  di   score   J(17)      J(342)      J(-342)\n");
    int best=0,ba=0,bb=0,bi=0;
    for(int da=-1;da<=1;da++) for(int db=-1;db<=1;db++) for(int di=-1;di<=1;di++){
        int s=score(da,db,di); if(s>best){best=s;ba=da;bb=db;bi=di;}
        printf("   %+d  %+d  %+d   %4d   %8lld   %9lld   %9lld%s\n",da,db,di,s,
            (long long)Jgen(17,da,db,di),(long long)Jgen(342,da,db,di),(long long)Jgen(-342,da,db,di),
            (da==1&&db==1&&di==0)?"   <- the 1s-consumed reading, on both operands":
            (da==0&&db==0&&di==0)?"   <- baseline":""); }
    printf("  best in this family: %d/1024 at da=%+d db=%+d di=%+d\n",best,ba,bb,bi);
    printf("  NOTE every row has the SAME J(17) and J(342): the hypothesis is sign-only, and both pinned\n");
    printf("  POSITIVE offsets are untouched at 45630 and 18466846 (they need 45617 and 18466836).\n");
    { // widen: allow the negative-side offset to be any value, still sign-only
      int bs=0,bd=0; for(int d=-64;d<=64;d++){ int s=score(d,d,0); if(s>bs){bs=s;bd=d;} }
      printf("  widening the negative-side offset to +-64 units of t: best %d/1024 at d=%+d\n",bs,bd); }
    printf("\n=== H2a: extra bit as a VALUE -- dx = (2t+e)<<12 ===\n");
    for(int e=-2;e<=2;e++){ auto J=[&](int t)->i64{ i64 dx=(i64)(2*t+e)<<12; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; };
        int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 v=J(i-512); if(v>=JL[i]&&v<=JH[i])s++; }
        printf("   e=%+d : %4d/1024   J(17)=%lld  J(342)=%lld  J(-342)=%lld\n",e,s,(long long)J(17),(long long)J(342),(long long)J(-342)); }
    printf("\n=== H2b: extra bit as a FIELD WIDTH -- dx sign-extended from W bits (independently per multiply) ===\n");
    printf("   W1  W2   score   J(17)      J(342)     J(-342)\n");
    int bw=0,bw1=0,bw2=0;
    for(int W1=20;W1<=26;W1++) for(int W2=20;W2<=26;W2++){ i64 a,b,c; int s=scoreW(W1,W2,a,b,c);
        if(s>bw){bw=s;bw1=W1;bw2=W2;}
        if(W1>=22&&W1<=24&&W2>=22&&W2<=24) printf("   %2d  %2d   %4d   %8lld  %9lld  %9lld%s\n",W1,W2,s,(long long)a,(long long)b,(long long)c,
            (W1==23&&W2==23)?"   <- |dx|max = 2^22, the model's own width":""); }
    printf("  best over W1,W2 in [20,26]: %d/1024 at W1=%d W2=%d\n",bw,bw1,bw2);
    return 0; }
