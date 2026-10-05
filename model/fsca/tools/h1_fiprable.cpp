// h1_fiprable.cpp -- can the FSCA square path's array calls be reproduced by FIPR on real hardware?
// FIPR presents the array with NORMALISED mantissas only: both operands always in [2^23, 2^24), positive.
// FSCA's operands are smaller and sometimes negative, so a literal reproduction is impossible.  But all of the
// array's FITTED behaviour -- the row truncation below column 16, the hot-one threshold, and the two group
// rules on rows 0-2 and 6-8 -- lives in the LOW rows.  If the total loss depends only on the low bits of the
// operands, then a FIPR call with matching low bits exercises exactly the same behaviour, and the fitted rules
// CAN be validated at the bit patterns FSCA actually produces.  Test that.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <map>
int main(){ build_tables();
    srand(12345);
    // loss(rec,mul) = arr(rec,mul) - exact product
    auto loss=[&](i64 r,i64 m){ return arr(r,m) - r*m; };
    puts("(a) does the array's LOSS depend only on the low bits of the operands?");
    for(int rb=16; rb<=22; rb+=2) for(int mb=14; mb<=18; mb+=2){
        int bad=0,n=0;
        for(int it=0; it<4000; it++){
            i64 r = (i64)(rand()%(1<<20)) + (1<<21);
            i64 m = (i64)(rand()%(1<<20)) + (1<<21);
            // a second pair agreeing on the low rb / mb bits but differing above
            i64 r2 = (r & ((1ll<<rb)-1)) | ((i64)(rand()%(1<<6)+2)<<rb);
            i64 m2 = (m & ((1ll<<mb)-1)) | ((i64)(rand()%(1<<6)+2)<<mb);
            if(loss(r,m)!=loss(r2,m2)) bad++;
            n++; }
        printf("    rec low %2d bits, mul low %2d bits : loss differs in %4d of %d pairs%s\n",
            rb,mb,bad,n, bad==0?"   <== loss is a function of these low bits alone":"");
    }
    puts("\n(b) which multiplicand residues does FSCA actually present?  dx = t<<13, so dx mod 2^16 = (t mod 8)*2^13:");
    { std::set<i64> res; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; res.insert(((dx%65536)+65536)%65536); }
      printf("    %zu distinct residues:", res.size()); for(i64 v:res) printf(" %lld",(long long)v); printf("\n"); }
    puts("\n(c) are matching FIPR operands reachable?  A FIPR mantissa is any value in [2^23, 2^24).");
    puts("    For a target (R, M) choose R' = 2^23 | (R mod 2^rb), M' = 2^23 | (M mod 2^mb): always in range.");
    { int ok=0,tot=0;
      for(int t=-512;t<=511;t+=37){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23;
          i64 Rp=(1ll<<23)|(((I1%(1ll<<18))+(1ll<<18))%(1ll<<18));
          i64 Mp=(1ll<<23)|(((dx%(1ll<<16))+(1ll<<16))%(1ll<<16));
          tot++; if(Rp>=(1ll<<23)&&Rp<(1ll<<24)&&Mp>=(1ll<<23)&&Mp<(1ll<<24)) ok++; }
      printf("    %d of %d sampled FSCA square-path calls have a FIPR-reachable low-bit twin\n",ok,tot); }
    puts("\n(d) what CANNOT be reached: FIPR's operands are always positive, so the sign handling");
    puts("    (negative recoded operand, negative multiplicand) is not testable by any FIPR sequence.");
    return 0; }
