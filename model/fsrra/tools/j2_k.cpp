// j2_k.cpp -- THE JOINT-FIT TEST.  FSCA cannot separate the square chain's rounding constant: K1 = 2^19,
// 2^19+2^16, 2^19+2*2^16 and 2^19+3*2^16 all score 147 there.  FSRRA runs the same array with the same
// conventions and has 512x more ground truth.  Scan the same constants here, with the coefficient table held
// fixed (nothing refitted), and see whether they separate.
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
struct Samp{ int par,n; i64 t; i64 q4; uint8_t cm; };
static std::vector<Samp> S;
static i64 REC1[34],REC2[34],REC3[34],C0T[34]; static int E1T[34],K2T[34];
static long score(i64 Ksq, i64 KI, i64 Kfin){
    long miss=0;
    for(const Samp& s : S){ int ix=s.par*17+s.n; i64 dx=s.t*16;
        i64 sqraw=dx*dx; i64 sq21=(sqraw+Ksq)>>23, sq19=(sqraw+Ksq)>>25;
        i64 I=(arr(REC3[ix],sq19)+KI)>>23;
        i64 W = C0T[ix] - sm(arr(REC1[ix],dx)+K, 26-E1T[ix])
                        + sm(arr(REC2[ix],sq21)+K, 25+K2T[ix])
                        - sm(arr(dx,I)+Kfin, 27);
        i64 d=W-s.q4; if(!(d>=0&&d<=3&&((s.cm>>d)&1))) miss++; }
    return miss; }
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):32;
    const int NA=1<<24; RZ.resize(NA); RN.resize(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb");
    if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA){puts("no rz");return 1;} fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb");
    if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA){puts("no rn");return 1;} fclose(f);
    for(int par=0;par<2;par++) for(int n=0;n<17;n++){ int ix=par*17+n; const int64_t*T=FSRRA_TAB[ix];
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24)) e1++; REC1[ix]=(c1>>e1)>>1; E1T[ix]=e1;
        i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22)) k2++; REC2[ix]=B2<<k2; K2T[ix]=k2;
        REC3[ix]=T[3]*128; C0T[ix]=T[0];
        i64 center=((i64)par<<23)+((i64)n<<19);
        for(i64 t=-(1<<18); t<(1<<18); t+=STEP){ i64 idx=center+t;
            if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23)) continue;
            u64 q=mant(RZ[idx]), rn=mant(RN[idx]); uint8_t cm=0;
            for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000)) cm|=1<<r; }
            if(!cm) continue; S.push_back({par,n,t,4*(i64)q,cm}); } }
    printf("samples: %zu (step %d)\n",S.size(),STEP);
    long b=score(K,K,K); printf("baseline (all three constants 2^19): %ld misses (%.4f%%)\n",b,100.0*b/S.size());
    puts("\nscan of the CUBIC CHAIN intermediate's constant K(I)   [FSCA cannot separate these]");
    for(int j=-2;j<=6;j++){ i64 KI=K+(i64)j*(1ll<<16); if(KI<0)continue; long m=score(K,KI,K);
        printf("  K(I) = 2^19 %+d*2^16 = %-8lld : %8ld misses (%.4f%%)%s\n",j,(long long)KI,m,100.0*m/S.size(),
            j==0?"   <- assumed":(j==1?"   <- what the FSRRA notes and FSCA both prefer":"")); }
    puts("\nscan of the SQUARE field's constant K(sq)");
    for(int j=-3;j<=4;j++){ i64 Ks=K+(i64)j*(1ll<<17); if(Ks<0)continue; long m=score(Ks,K,K);
        printf("  K(sq) = 2^19 %+d*2^17 = %-8lld : %8ld misses (%.4f%%)\n",j,(long long)Ks,m,100.0*m/S.size()); }
    puts("\nscan of the FINAL cubic product's constant");
    for(int j=-2;j<=4;j++){ i64 Kf=K+(i64)j*(1ll<<16); if(Kf<0)continue; long m=score(K,K,Kf);
        printf("  K(fin) = 2^19 %+d*2^16 = %-8lld : %8ld misses (%.4f%%)\n",j,(long long)Kf,m,100.0*m/S.size()); }
    return 0; }
