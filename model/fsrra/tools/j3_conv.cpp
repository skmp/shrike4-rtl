// j3_conv.cpp -- pin the SHARED datapath conventions with FSRRA's 16.7M samples.
// FSCA's square chain and FSRRA's cubic chain have the same shape: coefficient x offset -> latch -> x offset.
// So the latch rule and the sign-magnitude aligner are shared, and FSRRA has 512x the ground truth to pin them.
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
static const i64 K = 1ll<<19;
// alignment conventions for a signed product -> magnitude at unit u   (FSCA: rule 0 is uniquely pinned)
static inline i64 alg(i64 P,int u,int c){ switch(c){
    case 0: return P>=0? (P>>u) : -((~P)>>u);            // one's-complement magnitude  (FSCA's answer)
    case 1: return P>=0? (P>>u) : -((-P)>>u);            // toward zero
    case 2: return P>>u;                                  // two's-complement floor
    default: return P>=0? ((P+(1ll<<(u-1)))>>u) : -(((-P)+(1ll<<(u-1)))>>u); } }   // nearest
// latch rules for an intermediate
static inline i64 lat(i64 v,int s,int r){ switch(r){
    case 0: return v>>s;                                  // floor
    case 1: return v>=0? (v>>s) : -((-v)>>s);             // toward zero
    default: return v>=0? (v>>s) : -((~v)>>s); } }        // one's-complement magnitude
static inline u64 mant(uint32_t v){ return (v & 0x7fffff) | 0x800000; }
static inline u64 rne2(i64 W){ u64 q=W>>2; int r=W&3; return q + ((r>2)||(r==2&&(q&1))); }
static std::vector<uint32_t> RZ,RN;
struct Samp{ int ix; i64 t,q4; uint8_t cm; };
static std::vector<Samp> S;
static i64 REC1[34],REC2[34],REC3[34],C0T[34]; static int E1T[34],K2T[34];
static long score(int algc,int latr){ long miss=0;
    for(const Samp&s:S){ int ix=s.ix; i64 dx=s.t*16; i64 sq=dx*dx;
        i64 sq21=(sq+K)>>23, sq19=(sq+K)>>25;
        i64 I=lat(arr(REC3[ix],sq19)+K,23,latr);
        i64 W = C0T[ix] - alg(arr(REC1[ix],dx)+K, 26-E1T[ix], algc)
                        + alg(arr(REC2[ix],sq21)+K, 25+K2T[ix], algc)
                        - alg(arr(dx,I)+K, 27, algc);
        i64 d=W-s.q4; if(!(d>=0&&d<=3&&((s.cm>>d)&1))) miss++; }
    return miss; }
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):64;
    const int NA=1<<24; RZ.resize(NA); RN.resize(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb");
    if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA){puts("no rz");return 1;} fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb");
    if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA){puts("no rn");return 1;} fclose(f);
    for(int par=0;par<2;par++) for(int n=0;n<17;n++){ int ix=par*17+n; const int64_t*T=FSRRA_TAB[ix];
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24)) e1++; REC1[ix]=(c1>>e1)>>1; E1T[ix]=e1;
        i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22)) k2++; REC2[ix]=B2<<k2; K2T[ix]=k2;
        REC3[ix]=T[3]*128; C0T[ix]=T[0]; i64 center=((i64)par<<23)+((i64)n<<19);
        for(i64 t=-(1<<18); t<(1<<18); t+=STEP){ i64 idx=center+t;
            if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23)) continue;
            u64 q=mant(RZ[idx]), rn=mant(RN[idx]); uint8_t cm=0;
            for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000)) cm|=1<<r; }
            if(!cm) continue; S.push_back({ix,t,4*(i64)q,cm}); } }
    printf("samples %zu\n\naligner convention x latch rule  (misses, %% of samples)\n",S.size());
    const char* an[4]={"one's-complement magnitude","toward zero","two's-complement floor","nearest"};
    const char* ln[3]={"floor","toward zero","one's-complement"};
    printf("%-28s","aligner \\ latch"); for(int r=0;r<3;r++) printf("%16s",ln[r]); printf("\n");
    for(int c=0;c<4;c++){ printf("%-28s",an[c]);
        for(int r=0;r<3;r++){ long m=score(c,r); printf("%10ld %5.3f%%",m,100.0*m/S.size()); } printf("\n"); }
    return 0; }
