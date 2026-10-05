// rr2_sqround.cpp -- rr1: FSRRA's remaining misses are boundary cases of the SQUARE product, hardware one unit HIGH.
// Is it the rounding of the square latch?   sq21 = (arr(dx,dx) + Ksq) >> 23   (model: Ksq = 2^19, i.e. almost truncation)
// and/or the rounding constant of the square PRODUCT  T2 = sm(arr(rec2, sq21) + Kp, 25+k2)  (model: Kp = 2^19).
// All 34 nodes, stride STEP; REC2 allowed +-2 LSB per node (the published table is fitted at a coarse grid).
#include "../../fsca/tools/fsca_common.h"
#include "../sh4_fsrra_table.h"
#include <vector>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); }; int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
        i64 mag=(i64)(d<0?-d:d)*mul, row=d<0?~mag:mag; R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16; if(d<0&&2*k>=18)T+=1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2); int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16; return T; }
static inline i64 sm(i64 P,int u){ return P>=0?(P>>u):-((~P)>>u); }
static const i64 K=1ll<<19;
static inline u64 mant(uint32_t v){ return (v&0x7fffff)|0x800000; }
static inline u64 rne2(i64 W){ u64 q=W>>2; int r=W&3; return q+((r>2)||(r==2&&(q&1))); }
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):7; const int NA=1<<24; std::vector<uint32_t> RZ(NA),RN(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb"); if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb"); if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    i64 KSQ[]={0,1ll<<19,1ll<<20,1ll<<21,1ll<<22,(1ll<<22)+(1ll<<19),(1ll<<23)-1}; const char* kn[]={"0","2^19 (model)","2^20","2^21","2^22 (nearest)","2^22+2^19","2^23-1 (ceil)"};
    struct Smp{ i64 dx,sqr,q4; uint8_t cm; };
    long tot[7][3]={{0}}; long N=0;
    for(int par=0;par<2;par++)for(int n=0;n<17;n++){ const int64_t*T=FSRRA_TAB[par*17+n]; i64 center=((i64)par<<23)+((i64)n<<19);
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24))e1++; i64 rec1=(c1>>e1)>>1; i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22))k2++; i64 rec2=B2<<k2, rec3=T[3]*128, C0=T[0];
        std::vector<Smp> S; for(i64 t=-(1<<18);t<(1<<18);t+=STEP){ i64 idx=center+t; if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23))continue; u64 q=mant(RZ[idx]),rn=mant(RN[idx]); uint8_t cm=0; for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000))cm|=1<<r; } if(!cm)continue; i64 dx=t*16; S.push_back({dx,arr(dx,dx),(i64)(4*q),cm}); }
        N+=S.size(); std::vector<i64> base(S.size()); for(size_t i=0;i<S.size();i++){ auto&s=S[i]; i64 sq19=(s.sqr+K)>>25; base[i]=C0-sm(arr(rec1,s.dx)+K,26-e1)-sm(arr(s.dx,(arr(rec3,sq19)+K)>>23)+K,27); }
        for(int ki=0;ki<7;ki++) for(int kp=0;kp<3;kp++){ i64 Kp = kp==0?K : kp==1?0 : (1ll<<(24+k2)); int best=1<<30;
            for(int d2=-2;d2<=2;d2++){ int miss=0; for(size_t i=0;i<S.size();i++){ auto&s=S[i]; i64 sq21=(s.sqr+KSQ[ki])>>23; i64 W=base[i]+sm(arr(rec2+d2,sq21)+Kp,25+k2); i64 d=W-s.q4; if(!(d>=0&&d<=3&&((s.cm>>d)&1)))miss++; } if(miss<best)best=miss; }
            tot[ki][kp]+=best; } }
    printf("%ld samples over all 34 nodes (stride %d)\n  square-latch constant Ksq      | product constant: 2^19 (model)      0 (truncate)     half (nearest)\n",N,STEP);
    for(int ki=0;ki<7;ki++) printf("    %-24s   |      %8ld (%.4f%%)   %8ld (%.4f%%)   %8ld (%.4f%%)\n",kn[ki],tot[ki][0],100.0*tot[ki][0]/N,tot[ki][1],100.0*tot[ki][1]/N,tot[ki][2],100.0*tot[ki][2]/N);
    return 0; }
