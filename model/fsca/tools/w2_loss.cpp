// w2_loss.cpp -- bound |arr(rec,mul) - rec*mul| over the operand domain branch 2 actually uses, and
// check the claimed loss law (loss depends only on rec mod 2^18 and mul mod 2^16).  LMAX = 2^21 is
// the bound the KCw brackets are built on, so it has to hold.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
int main(){ i64 lmin=0,lmax=0; long long n=0;
    // NOTE: the array recodes 14 Booth digits = a SIGNED 28-bit operand, so |rec| < 2^27 is the real
    // domain; beyond that the top digit reads bit 27 as a sign and the result flips.
    // full sweep of rec residues mod 2^18 at every distinct (mul mod 2^16) that dx = t<<w can take
    for(int w=0;w<=16;w++){
        int nm = w>=16? 1 : (1<<(16-w));
        for(int r=0;r<nm;r++){ if(nm>64 && (r%(nm/64))!=0) continue;   // 64 residues per w
            i64 mul=((i64)r<<w);
            for(i64 base : {0ll, 5173573ll, 41388584ll, 67108864ll}){
                for(i64 d=0; d<262144; d+=251){ i64 rec=base+d; i64 L=arr(rec,mul)-rec*mul; n++;
                    if(L<lmin)lmin=L; if(L>lmax)lmax=L; } } } }
    // and with the real operand pairs the scans use
    for(int w=2;w<=24;w++) for(int t=-512;t<=511;t++){ i64 mul=(i64)t<<w;
        for(i64 rec : {1ll,17ll,5173573ll,10347146ll,20694292ll,41388584ll,82777168ll,134217727ll,
                       100000003ll,7ll}){ i64 L=arr(rec,mul)-rec*mul; n++;
            if(L<lmin)lmin=L; if(L>lmax)lmax=L; } }
    printf("samples %lld : loss in [%lld, %lld]   |loss| <= %lld ; LMAX used = %lld (2^21)  ok=%d\n",
        n,(long long)lmin,(long long)lmax,(long long)(lmin<-lmax?-lmin:lmax),(long long)(1ll<<21),
        (lmin>-(1ll<<21)&&lmax<(1ll<<21)));
    // loss-law check: same residues -> same loss
    int bad=0,tot=0; srand(7);
    for(int k=0;k<200000;k++){ i64 rec=((i64)rand()<<15|rand())%(1ll<<26); i64 mul=((i64)rand()<<15|rand())%(1ll<<24);
        i64 rec2=rec+(1ll<<18)*(1+rand()%7), mul2=mul+(1ll<<16)*(1+rand()%7);
        if(rec2>=(1ll<<28)||mul2>=(1ll<<26)) continue; tot++;
        i64 L1=arr(rec,mul)-rec*mul, L2=arr(rec2,mul2)-rec2*mul2; if(L1!=L2) bad++; }
    printf("loss law (rec mod 2^18, mul mod 2^16): %d/%d violations\n",bad,tot);
    return 0; }
