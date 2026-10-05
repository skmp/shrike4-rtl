// rr3_epscdf.cpp -- FSRRA's square-product error, measured like fsca/tools/rt22_epscdf.cpp and expressed in RAW product
// units so the two instructions can be compared.  For a sample whose model square product P2 = arr(REC2, sq21) + K lies a
// distance g below the next multiple of 2^(25+k2):   hw W = model+1 (A) or = model (B)  ->  P(A | g) = P(eps >= g).
// Same for a distance f above a multiple (hw = model-1).  Also per-node totals, to see which nodes carry the 0.14 %.
#include "../../fsca/tools/fsca_common.h"
#include "../sh4_fsrra_table.h"
#include <vector>
#include <cmath>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); }; int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
        i64 mag=(i64)(d<0?-d:d)*mul, row=d<0?~mag:mag; R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16; if(d<0&&2*k>=18)T+=1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2); int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16; return T; }
static inline i64 sm(i64 P,int u){ return P>=0?(P>>u):-((~P)>>u); }
static const i64 K=1ll<<19;
static inline u64 mant(uint32_t v){ return (v&0x7fffff)|0x800000; }
static inline u64 rne2(i64 W){ u64 q=W>>2; int r=W&3; return q+((r>2)||(r==2&&(q&1))); }
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):3; const int NA=1<<24; std::vector<uint32_t> RZ(NA),RN(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb"); if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb"); if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    const int NB=14; long Aup[NB]={0},Bup[NB]={0},Adn[NB]={0},Bdn[NB]={0};   // raw-distance bins: [2^(12+b), 2^(13+b))
    puts("par node k2 | samples  misses |  of which: hw high / hw low");
    for(int par=0;par<2;par++)for(int n=0;n<17;n++){ const int64_t*T=FSRRA_TAB[par*17+n]; i64 center=((i64)par<<23)+((i64)n<<19);
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24))e1++; i64 rec1=(c1>>e1)>>1; i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22))k2++; i64 rec2=B2<<k2, rec3=T[3]*128, C0=T[0]; int u=25+k2;
        long ns=0,miss=0,hi_=0,lo_=0;
        for(i64 t=-(1<<18);t<(1<<18);t+=STEP){ i64 idx=center+t; if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23))continue; u64 q=mant(RZ[idx]),rn=mant(RN[idx]); uint8_t cm=0; for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000))cm|=1<<r; } if(!cm)continue; ns++;
            i64 dx=t*16, sqr=arr(dx,dx), sq21=(sqr+K)>>23, sq19=(sqr+K)>>25; i64 P2=arr(rec2,sq21)+K; i64 W=C0-sm(arr(rec1,dx)+K,26-e1)+sm(P2,u)-sm(arr(dx,(arr(rec3,sq19)+K)>>23)+K,27); i64 d=W-4*(i64)q;
            auto ok=[&](i64 dd){ return dd>=0&&dd<=3&&((cm>>dd)&1); }; bool in0=ok(d),inP=ok(d+1),inM=ok(d-1); if(!in0){miss++; if(inP)hi_++; else if(inM)lo_++;}
            i64 fr=P2&((1ll<<u)-1); i64 g=(1ll<<u)-fr;   // distance below the next boundary / above the previous
            auto bin=[&](i64 x){ int b=(x<=0)?0:(int)floor(log2((double)x))-12; if(b<0)b=0; if(b>=NB)b=NB-1; return b; };
            if(!in0&&inP)Aup[bin(g)]++; else if(in0&&!inP)Bup[bin(g)]++;
            if(!in0&&inM)Adn[bin(fr)]++; else if(in0&&!inM)Bdn[bin(fr)]++; }
        printf(" %d   %2d   %d  | %7ld  %5ld  |   %5ld / %5ld\n",par,n,k2,ns,miss,hi_,lo_); }
    puts("\nraw distance of the model's square product to the boundary |  NEXT boundary above: hw=model+1  hw=model   P(eps >= g)  ||  boundary below: hw=model-1  hw=model   P(eps < -f)");
    for(int b=0;b<NB;b++) printf("      [2^%d, 2^%d)                                        |        %7ld   %9ld     %6.2f%%       ||        %7ld   %9ld     %6.2f%%\n",12+b,13+b,Aup[b],Bup[b],Aup[b]+Bup[b]?100.0*Aup[b]/(Aup[b]+Bup[b]):0.0,Adn[b],Bdn[b],Adn[b]+Bdn[b]?100.0*Adn[b]/(Adn[b]+Bdn[b]):0.0);
    return 0; }
