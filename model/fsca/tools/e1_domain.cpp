// e1_domain.cpp -- DIRECTION 2, step 1: what do the FIPR captures actually constrain about arr()?
// Two separate questions:
//   (a) WHICH COLUMNS of the array's internal sum T are observable through FIPR?
//   (b) WHICH OPERANDS does FIPR ever present to the array, and does FSCA stay inside that domain?
#include "base2.h"
#include "../../fipr/fipr_vecgen.h"
#include <cstdio>
#include <algorithm>
#include <set>
int main(){ build_tables();
    puts("(a) COLUMNS.  fipr_mult28 returns (T + 2^19) >> 20, so FIPR observes T only to within a 2^20-wide bin:");
    puts("    columns 0..19 of T are invisible except through the carry out of the +2^19.");
    puts("    BUT every FSCA use of the array is (T + 2^19) >> s with s >= 20 and the SAME constant, and");
    puts("    ((T+2^19)>>20)>>(s-20) == (T+2^19)>>s, so each one is a function of the FIPR-observable value:");
    printf("      T1 = sm(arr(C1f,dx)+K, 23-e1)   = mult28 >> (3-e1)\n");
    printf("      A3 = ((arr(t^2<<3,dx)+K)>>20)>>3 = mult28 >> 3\n");
    printf("      I1 = (arr(.,.)+K)>>23            = mult28 >> 3\n");
    printf("      J  = (arr(I1,dx)+K)>>20          = mult28\n");
    printf("      T2 = sm(arr(J,S)+K, u2)          = mult28 >> (u2-20)\n");
    puts("    So the low columns are NOT the unverified part.  The operand domain is.\n");
    // (b) FIPR's operand domain, straight from the vector generator the dump was built with
    uint32_t a[4],b[4]; u64 rmin=~0ull,rmax=0,mmin=~0ull,mmax=0; long nz=0,neg=0,tot=0;
    for(uint32_t i=0;i<FIPR_N;i+= (FIPR_N/200000)){ fipr_vec(i,a,b);
        for(int j=0;j<4;j++){ if(!a[j]&&!b[j]) continue;
            int ea=(a[j]>>23)&0xff, eb=(b[j]>>23)&0xff; if(!ea||!eb) continue;
            u64 rec=(b[j]&0x7fffff)|0x800000u, mul=(a[j]&0x7fffff)|0x800000u;
            rmin=std::min(rmin,rec); rmax=std::max(rmax,rec); mmin=std::min(mmin,mul); mmax=std::max(mmax,mul);
            tot++; } }
    printf("(b) FIPR OPERAND DOMAIN over the generator the 32M-vector dump was built from (%ld sampled products):\n",tot);
    printf("    recoded operand in [%llu, %llu]  = [2^%.2f, 2^%.2f]   always POSITIVE\n",
        (unsigned long long)rmin,(unsigned long long)rmax,log2((double)rmin),log2((double)rmax));
    printf("    multiplicand    in [%llu, %llu]  = [2^%.2f, 2^%.2f]   always POSITIVE\n",
        (unsigned long long)mmin,(unsigned long long)mmax,log2((double)mmin),log2((double)mmax));
    puts("    (every FIPR input is a NORMAL float, so both mantissas carry the implicit 1: always [2^23, 2^24).)");
    puts("    Signs never reach the array -- FIPR's datapath is sign-magnitude, signs are handled at the aligner.\n");
    // FSCA's array uses
    puts("(c) EVERY ARRAY USE IN THE FSCA MODEL, with its operand ranges:");
    i64 rlo[8],rhi[8],mlo[8],mhi[8]; int rneg[8]={0},mneg[8]={0};
    const char* nm[8]={"T1  arr(C1f, dx)","A3  arr(t^2<<3, dx)","T3  arr(c3<<5, A3)",
                       "I1  arr(KC, dx)","I1' arr(dx, KC)  [the 147 variant]","J   arr(I1, dx)","T2  arr(J, S)",""};
    for(int i=0;i<7;i++){ rlo[i]=mlo[i]=(i64)1<<62; rhi[i]=mhi[i]=-((i64)1<<62); }
    for(int n=0;n<=16;n++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20;
        i64 R[7]={TB[n].C1f,((i64)t*t)<<3,TB[n].c3<<5,KC,dx,I1,J};
        i64 M[7]={dx,dx,A3,dx,KC,dx,TB[n].S};
        for(int i=0;i<7;i++){ rlo[i]=std::min(rlo[i],R[i]); rhi[i]=std::max(rhi[i],R[i]);
            mlo[i]=std::min(mlo[i],M[i]); mhi[i]=std::max(mhi[i],M[i]);
            if(R[i]<0) rneg[i]=1; if(M[i]<0) mneg[i]=1; } }
    puts("    use                            recoded operand range          multiplicand range         outside FIPR?");
    for(int i=0;i<7;i++){
        bool ok = rlo[i]>=(1ll<<23)&&rhi[i]<(1ll<<24)&&mlo[i]>=(1ll<<23)&&mhi[i]<(1ll<<24);
        printf("    %-30s [%11lld,%10lld]%s [%11lld,%10lld]%s  %s\n",nm[i],
            (long long)rlo[i],(long long)rhi[i],rneg[i]?" NEG":"    ",
            (long long)mlo[i],(long long)mhi[i],mneg[i]?" NEG":"    ", ok?"inside":"** OUTSIDE **"); }
    puts("\n(d) THE CONCLUSION:");
    puts("    Not one FSCA array use lies inside FIPR's validated domain: every operand is smaller than 2^23,");
    puts("    and several are NEGATIVE.  A negative MULTIPLICAND is at least validated by FSCA itself -- nodes 0");
    puts("    and 32 are exact for all 1024 offsets and use arr(C1f,dx), arr(t^2<<3,dx), arr(c3<<5,A3) with");
    puts("    negative dx and A3.  A negative RECODED OPERAND is validated NOWHERE: it occurs in exactly one");
    puts("    place in the entire model -- arr(I1, dx) in the square chain -- which is the one unexplained term.");
    return 0; }
