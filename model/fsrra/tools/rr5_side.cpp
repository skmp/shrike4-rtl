// rr5_side.cpp -- does FSRRA's square-product excess belong to the OPERAND or to the PRODUCT?
// Every node sees the same offsets t (same dx, same square latch) against a different ROM field REC2_n.  For each t
// collect, over the nodes, the constraints on the excess eps_n of the square product:
//      hw = model+1 (pinned)  ->  eps_n >= g_n        hw = model (pinned)  ->  eps_n < g_n       (g_n = raw gap to the boundary)
//   O: eps_n = delta(t) * REC2_n   (the computed operand is larger by delta LSBs)      -> intersect in delta
//   P: eps_n = e(t)                (the same raw excess in every node's product)       -> intersect in e
//   U: eps_n = u(t) * 2^(25+k2_n)  (the same excess in OUTPUT units)                   -> intersect in u
// Count offsets with an EMPTY intersection, among the offsets where at least one node shows hw = model+1.
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
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):1; const int NA=1<<24; std::vector<uint32_t> RZ(NA),RN(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb"); if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb"); if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    struct Nd{ i64 C0,rec1,rec2,rec3,center; int e1,k2; bool use; }; std::vector<Nd> ND;
    for(int par=0;par<2;par++)for(int n=0;n<17;n++){ const int64_t*T=FSRRA_TAB[par*17+n]; i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24))e1++; i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22))k2++;
        bool use=!((par==0&&n==0)||(par==1&&n==2)||(par==1&&n==16)||n==16||n==0); ND.push_back({T[0],(c1>>e1)>>1,B2<<k2,T[3]*128,((i64)par<<23)+((i64)n<<19),e1,k2,use}); }
    long nt=0,active=0,emptyO=0,emptyP=0,emptyU=0; std::vector<double> dO; long multi=0;
    for(i64 t=-(1<<18);t<(1<<18);t+=STEP){ i64 dx=t*16, sqr=arr(dx,dx), sq21=(sqr+K)>>23, sq19=(sqr+K)>>25; nt++;
        double loO=0,hiO=1e30,loP=0,hiP=1e30,loU=0,hiU=1e30; int nflip=0;
        for(auto&nd:ND){ if(!nd.use)continue; i64 idx=nd.center+t; u64 q=mant(RZ[idx]),rn=mant(RN[idx]); uint8_t cm=0; for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000))cm|=1<<r; } if(!cm)continue;
            int u=25+nd.k2; i64 P2=arr(nd.rec2,sq21)+K; i64 W=nd.C0-sm(arr(nd.rec1,dx)+K,26-nd.e1)+sm(P2,u)-sm(arr(dx,(arr(nd.rec3,sq19)+K)>>23)+K,27); i64 d=W-4*(i64)q;
            auto ok=[&](i64 dd){ return dd>=0&&dd<=3&&((cm>>dd)&1); }; bool in0=ok(d),inP=ok(d+1); double g=(double)((1ll<<u)-(P2&((1ll<<u)-1)));
            if(!in0&&inP){ nflip++; loO=std::max(loO,g/nd.rec2); loP=std::max(loP,g); loU=std::max(loU,g/(double)(1ll<<u)); }
            else if(in0&&!inP){ hiO=std::min(hiO,g/nd.rec2); hiP=std::min(hiP,g); hiU=std::min(hiU,g/(double)(1ll<<u)); } }
        if(!nflip)continue; active++; if(nflip>1)multi++; if(loO>=hiO)emptyO++; else dO.push_back(loO); if(loP>=hiP)emptyP++; if(loU>=hiU)emptyU++; }
    printf("%ld offsets, %ld with at least one node showing hw = model+1 (%ld of them with two or more such nodes)\n",nt,active,multi);
    printf("  EMPTY intersections:   O (operand-side, eps = delta*REC2): %ld     P (same raw excess): %ld     U (same excess in output units): %ld\n",emptyO,emptyP,emptyU);
    if(!dO.empty()){ std::sort(dO.begin(),dO.end()); printf("  required delta (lower bound, operand LSBs): median %.3f   90%% %.3f   99%% %.3f   max %.3f\n",dO[dO.size()/2],dO[dO.size()*9/10],dO[dO.size()*99/100],dO.back()); }
    return 0; }
