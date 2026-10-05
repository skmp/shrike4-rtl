// h2_twin.cpp -- the decisive test of "can FSCA's array calls be decomposed into FIPR tests?"
// All of the array's FITTED behaviour is the LOSS = arr(rec,mul) - rec*mul: the row truncation below column 16,
// the hot-one threshold, and the two group rules.  If the loss is a function of the operands' LOW bits alone,
// then a FIPR call with the same low bits exercises exactly the same rules -- and FIPR mantissas can carry any
// low bits, because the only constraint is the implicit leading 1 at bit 23.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <set>
static inline i64 lossOf(i64 r,i64 m){ return arr(r,m) - r*m; }
int main(){ build_tables(); srand(999);
    puts("(a) is the loss a function of (rec mod 2^a, mul mod 2^b)?  operands kept inside the array's width.");
    for(int a=14;a<=20;a+=2) for(int b=12;b<=18;b+=2){
        int bad=0,n=0;
        for(int it=0;it<20000;it++){
            i64 r=(i64)rand()%(1ll<<23), m=(i64)rand()%(1ll<<23);
            i64 r2=(r & ((1ll<<a)-1)) | ((i64)(rand()%(1<<(23-a)))<<a);
            i64 m2=(m & ((1ll<<b)-1)) | ((i64)(rand()%(1<<(23-b)))<<b);
            if(lossOf(r,m)!=lossOf(r2,m2)) bad++; n++; }
        printf("    rec low %2d, mul low %2d : %5d differing of %d%s\n",a,b,bad,n,
            bad==0?"   <== loss determined by these bits":""); }
    puts("\n(b) the real question: for EVERY array call the FSCA square path makes, does a FIPR-reachable");
    puts("    operand pair with the same low bits have the same loss?  (rec' = 2^23|low18, mul' = 2^23|low16)");
    long tot=0, same=0, negrec=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
        struct P{ i64 r,m; } calls[3] = {{KC,dx},{I1,dx},{J,TB[8].S}};
        for(auto&c:calls){ i64 r=c.r,m=c.m; tot++; if(r<0) negrec++;
            i64 rp=(1ll<<23)|(((r%(1ll<<18))+(1ll<<18))%(1ll<<18));
            i64 mp=(1ll<<23)|(((m%(1ll<<16))+(1ll<<16))%(1ll<<16));
            if(lossOf(r,m)==lossOf(rp,mp)) same++; } }
    printf("    %ld of %ld calls have a FIPR-reachable twin with the IDENTICAL loss  (%ld of them have a negative rec)\n",
        same,tot,negrec);
    puts("\n(c) how many distinct FIPR vectors would cover the whole square path?");
    { std::set<std::pair<i64,i64>> tw;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
        i64 rr[3]={KC,I1,J}, mm[3]={dx,dx,TB[8].S};
        for(int i=0;i<3;i++){ i64 rp=(1ll<<23)|(((rr[i]%(1ll<<18))+(1ll<<18))%(1ll<<18));
            i64 mp=(1ll<<23)|(((mm[i]%(1ll<<16))+(1ll<<16))%(1ll<<16)); tw.insert({rp,mp}); } } 
      printf("    %zu distinct (rec', mul') pairs -- a dump of that size is trivial to capture\n",tw.size()); }
    return 0; }
