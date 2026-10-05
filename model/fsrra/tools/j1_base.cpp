// j1_base.cpp -- clean FSRRA harness for the JOINT (FSCA + FSRRA) programme.
// Loads both dumps (RZ over [1,4) and the matching RN mantissas), pins the internal value per sample, and
// scores the structural model with the coefficient fields derived from the table by the stated rules --
// nothing refitted.  Then swaps the squarer: the model uses an EXACT t*t, but FSCA proved the hardware's
// square operand comes out of the FIPR array (arr(dx,dx) is short by 2^17 or 2^18 for 87.5% of offsets and it
// is the +2^19 that rescues it).  This is the first cross-instruction transfer.
#include "../../fsca/tools/fsca_common.h"
#include "../sh4_fsrra_table.h"
#include <vector>
#include <cstring>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d = -2*bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag=(i64)(d<0?-d:d)*mul, row=d<0?~mag:mag; R[k]=row*(1ll<<(2*k)); T += (R[k]>>16)<<16;
        if (d<0 && 2*k>=18) T += 1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T += (1ll<<16)*(m15>=2);
    int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T -= (1ll<<16)*p16; return T; }
static inline i64 sm(i64 P,int u){ return P>=0 ? (P>>u) : -((~P)>>u); }
static const i64 K = 1ll<<19;
static inline u64 mant(uint32_t v){ return (v & 0x7fffff) | 0x800000; }
static inline u64 rne2(i64 W){ u64 q=W>>2; int r=W&3; return q + ((r>2)||(r==2&&(q&1))); }
static std::vector<uint32_t> RZ, RN;
int main(int argc,char**argv){
    int STEP = argc>1? atoi(argv[1]) : 64;
    int SQMODE = argc>2? atoi(argv[2]) : 0;   // 0 = exact t*t (current model), 1..n = array squarer variants
    const int NA = 1<<24; RZ.resize(NA); RN.resize(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb");
    if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA){ puts("cannot read fsrra_rz.bin"); return 1; } fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb");
    if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA){ puts("cannot read the RN mantissa file"); return 1; } fclose(f);
    long totMiss=0, totN=0;
    printf("squarer mode %d, step %d\n",SQMODE,STEP);
    printf("par node |  samples  misses    %%\n");
    for(int par=0;par<2;par++) for(int n=0;n<17;n++){
        const int64_t* T = FSRRA_TAB[par*17+n]; i64 center=((i64)par<<23)+((i64)n<<19);
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24)) e1++; i64 rec1=(c1>>e1)>>1;
        i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22)) k2++; i64 rec2=B2<<k2, rec3=T[3]*128, C0=T[0];
        long miss=0,N=0;
        for(i64 t=-(1<<18); t<(1<<18); t+=STEP){ i64 idx=center+t;
            if(idx < ((i64)par<<23) || idx >= ((i64)(par+1)<<23)) continue;
            u64 q=mant(RZ[idx]), rn=mant(RN[idx]);
            uint8_t cm=0; for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn || (rn==0x800000 && v==0x1000000)) cm|=1<<r; }
            if(!cm) continue;                       // inconsistent sample (exponent boundary) -- skip
            i64 dx=t*16;
            // note t*t*256 == dx*dx exactly, so mode 0 and mode 1 differ ONLY by the array's truncation
            i64 sqraw = (SQMODE==0) ? dx*dx : arr(dx,dx);
            i64 KS = (SQMODE==2) ? 0 : (SQMODE==3 ? (1ll<<18) : K);
            i64 sq21 = (sqraw+KS)>>23;
            i64 sq19 = (sqraw+KS)>>25;
            i64 W = C0 - sm(arr(rec1,dx)+K, 26-e1)
                       + sm(arr(rec2,sq21)+K, 25+k2)
                       - sm(arr(dx, (arr(rec3,sq19)+K)>>23) + K, 27);
            i64 d=W-4*(i64)q; N++;
            if(!(d>=0 && d<=3 && ((cm>>d)&1))) miss++; }
        totMiss+=miss; totN+=N;
        printf(" %d   %2d  | %8ld %7ld  %.4f\n",par,n,N,miss,100.0*miss/(N?N:1)); }
    printf("TOTAL %ld misses of %ld samples (%.4f%%)\n",totMiss,totN,100.0*totMiss/totN);
    return 0; }
