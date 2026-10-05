// rr_common.h -- shared by the rr6+ FSRRA tools: the FIPR array, the aligner rule, the two dumps, per-node samples with
// the pinned internal value, and the structural model of j1_base.cpp (array squarer, = rtl/fpu_vec).
#pragma once
#include "../../fsca/tools/fsca_common.h"
#include "../sh4_fsrra_table.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <string>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); }; int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
        i64 mag=(i64)(d<0?-d:d)*mul, row=d<0?~mag:mag; R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16; if(d<0&&2*k>=18)T+=1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2); int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16; return T; }
static inline i64 sm(i64 P,int u){ return P>=0?(P>>u):-((~P)>>u); }
static const i64 K=1ll<<19;
static inline u64 mant(uint32_t v){ return (v&0x7fffff)|0x800000; }
static inline u64 rne2(i64 W){ u64 q=W>>2; int r=W&3; return q+((r>2)||(r==2&&(q&1))); }
static std::vector<uint32_t> RZ, RN;
static inline bool load_fsrra(){ const int NA=1<<24; RZ.resize(NA); RN.resize(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb"); if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA){puts("cannot read fsrra_rz.bin");return false;} fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb"); if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA){puts("cannot read the RN mantissa file");return false;} fclose(f); return true; }
// fields of one node word, as the structural model uses them
struct Word { i64 C0, rec1, rec2, rec3; int e1, k2; };
static inline Word word_from_table(int par,int n){ const int64_t*T=FSRRA_TAB[par*17+n]; Word w; i64 c1=-T[1]; w.e1=0; while((c1>>w.e1)>=(1ll<<24))w.e1++; w.rec1=(c1>>w.e1)>>1;
    i64 B2=4*T[2]; w.k2=0; while((B2<<w.k2)<(1ll<<22))w.k2++; w.rec2=B2<<w.k2; w.rec3=T[3]*128; w.C0=T[0]; return w; }
// pinned sample: q4 = 4 * RZ mantissa, cm = mask of the allowed low two bits (W - q4 in 0..3)
struct Smp { i64 t, dx, sq21, sq19, q4; uint8_t cm; };
static inline bool pin(i64 idx, i64& q4, uint8_t& cm){ u64 q=mant(RZ[idx]),rn=mant(RN[idx]); cm=0; for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000))cm|=1<<r; } q4=4*(i64)q; return cm!=0; }
static inline std::vector<Smp> node_samples(int par,int n,int step=1,int dxsh=4){ std::vector<Smp> S; i64 center=((i64)par<<23)+((i64)n<<19);
    for(i64 t=-(1<<18);t<(1<<18);t+=step){ i64 idx=center+t; if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23))continue; i64 q4; uint8_t cm; if(!pin(idx,q4,cm))continue;
        i64 dx=t<<dxsh, sqr=arr(dx,dx); S.push_back({t,dx,(sqr+K)>>23,(sqr+K)>>25,q4,cm}); } return S; }
static inline i64 termT1(const Word&w,const Smp&s){ return sm(arr(w.rec1,s.dx)+K,26-w.e1); }
static inline i64 termT2(const Word&w,const Smp&s){ return sm(arr(w.rec2,s.sq21)+K,25+w.k2); }
static inline i64 termT3(const Word&w,const Smp&s){ return sm(arr(s.dx,(arr(w.rec3,s.sq19)+K)>>23)+K,27); }
static inline i64 modelW(const Word&w,const Smp&s){ return w.C0-termT1(w,s)+termT2(w,s)-termT3(w,s); }
static inline bool okd(const Smp&s,i64 d){ return d>=0&&d<=3&&((s.cm>>d)&1); }
static inline bool load_words(const char*fn, Word*REF){ FILE*f=fopen(fn,"r"); if(!f){printf("cannot read %s\n",fn);return false;} int par,n,e1,k2,ms; long long c0,r1,r2,r3; size_t ns; int got=0;
    while(fscanf(f,"%d %d %lld %lld %d %lld %d %lld %d %zu",&par,&n,&c0,&r1,&e1,&r2,&k2,&r3,&ms,&ns)==10){ REF[par*17+n]={c0,r1,r2,r3,e1,k2}; got++; } fclose(f); return got==34; }
