// h3_verify.cpp -- verify the decomposition  arr(rec,mul) = rec*mul + loss(rec mod 2^18, mul mod 2^16)
// over the operand ranges FSCA actually uses, INCLUDING negative operands -- which is the case FIPR can
// never present directly, and the one direction 2 flagged as unvalidated.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <set>
int main(){ build_tables(); srand(4242);
    struct Case{ const char* name; i64 rlo,rhi,mlo,mhi; } cs[] = {
        {"both positive, FSCA-sized",        0, 1<<23,        0, 1<<23},
        {"negative multiplicand (dx<0)",     0, 1<<23,   -(1<<23), 0},
        {"negative recoded operand (I1<0)", -(1<<22), 0,        0, 1<<23},
        {"both negative",                   -(1<<22), 0,  -(1<<23), 0},
    };
    for(auto&c:cs){ long bad=0,n=0;
        for(int it=0;it<200000;it++){
            i64 r=c.rlo+(i64)(rand()%(int)(c.rhi-c.rlo));
            i64 m=c.mlo+(i64)(rand()%(int)(c.mhi-c.mlo));
            i64 rp=(1ll<<23)|(((r%(1ll<<18))+(1ll<<18))%(1ll<<18));
            i64 mp=(1ll<<23)|(((m%(1ll<<16))+(1ll<<16))%(1ll<<16));
            if((arr(r,m)-r*m) != (arr(rp,mp)-rp*mp)) bad++;
            n++; }
        printf("  %-34s : %6ld of %ld differ%s\n",c.name,bad,n, bad?"":"   <== decomposition HOLDS"); }
    puts("\n  so arr(rec,mul) = rec*mul + loss(rec mod 2^18, mul mod 2^16) with the SAME loss table for");
    puts("  negative operands -- the sign only enters the exact product, which needs no validation.");
    // emit the vector list for a hardware run
    FILE*f=fopen("h_fipr_vectors.txt","w");
    fprintf(f,"# FIPR test vectors that exercise the array's loss table at exactly the operand patterns the\n"
              "# FSCA square path produces.  Each line: rec_mantissa mul_mantissa (both 24-bit, implicit 1 at\n"
              "# bit 23 already included).  Build floats as (127<<23)|(mant & 0x7fffff) and run\n"
              "#   fipr fv0,fv4  with a[0] = mul_float, b[0] = rec_float, other lanes zero,\n"
              "# under FPSCR.RM = nearest and = zero, exactly as kos_fpu_dump/fipr_dump.c does.\n");
    std::set<std::pair<i64,i64>> tw;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
        i64 rr[3]={KC,I1,J}, mm[3]={dx,dx,TB[8].S};
        for(int i=0;i<3;i++){ i64 rp=(1ll<<23)|(((rr[i]%(1ll<<18))+(1ll<<18))%(1ll<<18));
            i64 mp=(1ll<<23)|(((mm[i]%(1ll<<16))+(1ll<<16))%(1ll<<16)); tw.insert({rp,mp}); } }
    for(auto&p:tw) fprintf(f,"%lld %lld\n",(long long)p.first,(long long)p.second);
    fclose(f);
    printf("\n  wrote h_fipr_vectors.txt: %zu vectors\n",tw.size());
    return 0; }
