// rr1_state.cpp -- where does FSRRA stand, and is it worth pursuing for the FSCA even path?
// Structural model of j1_base.cpp with the ARRAY squarer (= rtl/fpu_vec).  Per selected node:
//   (1) misses with the published (coarsely fitted) table;  (2) after a full-resolution coordinate refit of C0, REC1, REC2, REC3;
//   (3) for the misses that remain: which product sits next to a truncation boundary (fraction within 0.05 of 0 or 1),
//       and in which direction the hardware differs.
#include "../../fsca/tools/fsca_common.h"
#include "../sh4_fsrra_table.h"
#include <vector>
#include <cmath>
#include <algorithm>
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
    int sel[][2]={{0,1},{0,5},{0,9},{0,13},{1,3},{1,8},{1,12}};
    puts("par node | samples | misses: table  refit | refit moved: dC0 dREC1 dREC2 dREC3 | remaining misses nearest to a boundary of: linear square cubic none | hw above / below model");
    for(auto&pn:sel){ int par=pn[0],n=pn[1]; const int64_t*T=FSRRA_TAB[par*17+n]; i64 center=((i64)par<<23)+((i64)n<<19);
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24))e1++; i64 rec1=(c1>>e1)>>1; i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22))k2++; i64 rec2=B2<<k2, rec3=T[3]*128, C0=T[0];
        struct Smp{ i64 dx,sq21,sq19,q4; uint8_t cm; }; std::vector<Smp> S;
        for(i64 t=-(1<<18);t<(1<<18);t+=STEP){ i64 idx=center+t; if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23))continue; u64 q=mant(RZ[idx]),rn=mant(RN[idx]); uint8_t cm=0; for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000))cm|=1<<r; } if(!cm)continue;
            i64 dx=t*16; i64 sqr=arr(dx,dx); S.push_back({dx,(sqr+K)>>23,(sqr+K)>>25,(i64)(4*q),cm}); }
        auto eval=[&](i64 c0,i64 r1,i64 r2,i64 r3,int lim){ int miss=0; for(auto&s:S){ i64 W=c0-sm(arr(r1,s.dx)+K,26-e1)+sm(arr(r2,s.sq21)+K,25+k2)-sm(arr(s.dx,(arr(r3,s.sq19)+K)>>23)+K,27); i64 d=W-s.q4; if(!(d>=0&&d<=3&&((s.cm>>d)&1))){ if(++miss>=lim)break; } } return miss; };
        int m0=eval(C0,rec1,rec2,rec3,1<<30); int best=m0; i64 b0=C0,b1=rec1,b2=rec2,b3=rec3;
        for(int round=0;round<3;round++){
            for(i64 st: {1024,256,64,16,4,1}) for(int rep=0;rep<6;rep++){ bool mv=false; for(int sg=-1;sg<=1;sg+=2){ int m=eval(b0,b1,b2+sg*st,b3,best); if(m<best){best=m;b2+=sg*st;mv=true;} } if(!mv)break; }
            for(i64 st: {4096,1024,256,64,16,4,1}) for(int rep=0;rep<6;rep++){ bool mv=false; for(int sg=-1;sg<=1;sg+=2){ int m=eval(b0,b1,b2,b3+sg*st,best); if(m<best){best=m;b3+=sg*st;mv=true;} } if(!mv)break; }
            for(int d=-3;d<=3;d++){ int m=eval(b0,b1+d,b2,b3,best); if(m<best){best=m;b1+=d;} }
            for(int d=-3;d<=3;d++){ int m=eval(b0+d,b1,b2,b3,best); if(m<best){best=m;b0+=d;} } }
        int near[4]={0},above=0,below=0;
        for(auto&s:S){ i64 P1=arr(b1,s.dx)+K, P2=arr(b2,s.sq21)+K, P3=arr(s.dx,(arr(b3,s.sq19)+K)>>23)+K; i64 W=b0-sm(P1,26-e1)+sm(P2,25+k2)-sm(P3,27); i64 d=W-s.q4; if(d>=0&&d<=3&&((s.cm>>d)&1))continue;
            // direction: is the nearest allowed value above or below the model?
            int lo=4,hi=-1; for(int r=0;r<4;r++) if((s.cm>>r)&1){ if(r<lo)lo=r; if(r>hi)hi=r; } if(d<lo)above++; else below++;
            auto fr=[&](i64 P,int u){ i64 a=P<0?~P:P; double x=(double)(a&((1ll<<u)-1))/(double)(1ll<<u); return std::min(x,1-x); };
            double f1=fr(P1,26-e1),f2=fr(P2,25+k2),f3=fr(P3,27); double mn=std::min(f1,std::min(f2,f3)); if(mn>0.05)near[3]++; else if(mn==f1)near[0]++; else if(mn==f2)near[1]++; else near[2]++; }
        printf(" %d   %2d  | %6zu  |   %5d  %5d     |      %+3lld   %+3lld  %+6lld %+6lld         |        %4d  %4d  %4d  %4d                                  |  %d / %d\n",par,n,S.size(),m0,best,(long long)(b0-C0),(long long)(b1-rec1),(long long)(b2-rec2),(long long)(b3-rec3),near[0],near[1],near[2],near[3],above,below); fflush(stdout); }
    return 0; }
