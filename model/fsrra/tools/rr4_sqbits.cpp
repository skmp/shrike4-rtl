// rr4_sqbits.cpp -- rr3: FSRRA's hardware square product exceeds the model's by eps in [0, ~2^21) raw, never the other way.
// First candidate: the square OPERAND carries more (or fewer) bits than the model's 21.
//   sq_e = (arr(dx,dx) + Ksq) >> (23 - e) ;  T2 = sm(arr(REC2, sq_e) + K, 25 + k2 + e)     e = -1..4,  Ksq in {0, 2^19}
// and the role swap (square recoded, REC2 multiplicand).  All nodes except the three with known table-field problems
// (par0 n0, par1 n2, par1 n16).  REC2 refit +-2 LSB per node for each variant.
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
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):5; const int NA=1<<24; std::vector<uint32_t> RZ(NA),RN(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb"); if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb"); if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    long tot[6][2][2]={{{0}}}; long N=0; struct Smp{ i64 sqr,base,q4; uint8_t cm; };
    for(int par=0;par<2;par++)for(int n=0;n<17;n++){ if((par==0&&n==0)||(par==1&&n==2)||(par==1&&n==16))continue; const int64_t*T=FSRRA_TAB[par*17+n]; i64 center=((i64)par<<23)+((i64)n<<19);
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24))e1++; i64 rec1=(c1>>e1)>>1; i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22))k2++; i64 rec2=B2<<k2, rec3=T[3]*128, C0=T[0];
        std::vector<Smp> S; for(i64 t=-(1<<18);t<(1<<18);t+=STEP){ i64 idx=center+t; if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23))continue; u64 q=mant(RZ[idx]),rn=mant(RN[idx]); uint8_t cm=0; for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000))cm|=1<<r; } if(!cm)continue;
            i64 dx=t*16, sqr=arr(dx,dx), sq19=(sqr+K)>>25; S.push_back({sqr,C0-sm(arr(rec1,dx)+K,26-e1)-sm(arr(dx,(arr(rec3,sq19)+K)>>23)+K,27),(i64)(4*q),cm}); }
        N+=S.size();
        for(int ei=0;ei<6;ei++){ int e=ei-1; for(int ks=0;ks<2;ks++) for(int role=0;role<2;role++){ int best=1<<30;
            for(int d2=-2;d2<=2;d2++){ int miss=0; for(auto&s:S){ i64 sq=(s.sqr+(ks?K:0))>>(23-e); i64 P=role? arr(sq,rec2+d2):arr(rec2+d2,sq); i64 W=s.base+sm(P+K,25+k2+e); i64 d=W-s.q4; if(!(d>=0&&d<=3&&((s.cm>>d)&1)))miss++; } if(miss<best)best=miss; }
            tot[ei][ks][role]+=best; } } }
    printf("%ld samples, 31 nodes, stride %d\n square operand bits | Ksq=0: REC2 recoded / square recoded | Ksq=2^19: REC2 recoded / square recoded\n",N,STEP);
    for(int ei=0;ei<6;ei++) printf("      %2d%s        |      %6ld      /   %6ld          |       %6ld      /   %6ld\n",21+ei-1,ei==1?" (model)":"        ",tot[ei][0][0],tot[ei][0][1],tot[ei][1][0],tot[ei][1][1]);
    return 0; }
