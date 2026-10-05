// j4_attr.cpp -- is FSRRA's residual the SAME phenomenon as FSCA's?
// FSCA: the whole residual sits in one node-independent square-path latch, a free per-t value explains
// everything, and the required value is provably asymmetric in t.  FSRRA's node-independent latch is the
// square field sq21 = (dx^2 + K) >> 23 (it depends only on t, and all 34 nodes share it), so run the identical
// attribution test here -- with 512x finer t sampling than FSCA can offer.
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
static inline i64 sm(i64 P,int u){ return P>=0? (P>>u) : -((~P)>>u); }
static const i64 K=1ll<<19;
static inline u64 mant(uint32_t v){ return (v&0x7fffff)|0x800000; }
static inline u64 rne2(i64 W){ u64 q=W>>2; int r=W&3; return q+((r>2)||(r==2&&(q&1))); }
static std::vector<uint32_t> RZ,RN;
static i64 REC1[34],REC2[34],REC3[34],C0T[34]; static int E1T[34],K2T[34];
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):256; int RAD=argc>2?atoi(argv[2]):6;
    const int NA=1<<24; RZ.resize(NA); RN.resize(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb");
    if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA){puts("no rz");return 1;} fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb");
    if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA){puts("no rn");return 1;} fclose(f);
    for(int par=0;par<2;par++) for(int n=0;n<17;n++){ int ix=par*17+n; const int64_t*T=FSRRA_TAB[ix];
        i64 c1=-T[1]; int e1=0; while((c1>>e1)>=(1ll<<24)) e1++; REC1[ix]=(c1>>e1)>>1; E1T[ix]=e1;
        i64 B2=4*T[2]; int k2=0; while((B2<<k2)<(1ll<<22)) k2++; REC2[ix]=B2<<k2; K2T[ix]=k2;
        REC3[ix]=T[3]*128; C0T[ix]=T[0]; }
    long base=0,freeSq=0,freeBoth=0,nt=0,ns=0; long asym=0,asymT=0;
    // evaluate W for a given t, node, and offsets on the two shared square fields
    auto Wof=[&](int ix,i64 t,int d21,int d19)->i64{ i64 dx=t*16, sq=dx*dx;
        i64 sq21=((sq+K)>>23)+d21, sq19=((sq+K)>>25)+d19;
        return C0T[ix] - sm(arr(REC1[ix],dx)+K,26-E1T[ix])
                       + sm(arr(REC2[ix],sq21)+K,25+K2T[ix])
                       - sm(arr(dx,(arr(REC3[ix],sq19)+K)>>23)+K,27); };
    for(i64 a=0; a<(1<<18); a+=STEP){
      for(int sg=0; sg<2; sg++){ i64 t = sg? -a : a; if(sg&&a==0) continue;
        // collect the constraints over all 34 nodes at this t
        struct C{ int ix; i64 q4; uint8_t cm; }; std::vector<C> cs;
        for(int par=0;par<2;par++) for(int n=0;n<17;n++){ int ix=par*17+n;
            i64 idx=((i64)par<<23)+((i64)n<<19)+t;
            if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23)) continue;
            u64 q=mant(RZ[idx]), rn=mant(RN[idx]); uint8_t cm=0;
            for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000)) cm|=1<<r; }
            if(!cm) continue; cs.push_back({ix,4*(i64)q,cm}); }
        if(cs.empty()) continue; nt++; ns+=cs.size();
        auto miss=[&](int d21,int d19){ int m=0; for(auto&c:cs){ i64 W=Wof(c.ix,t,d21,d19); i64 d=W-c.q4;
            if(!(d>=0&&d<=3&&((c.cm>>d)&1))) m++; } return m; };
        int b0=miss(0,0); base+=b0;
        int bs=b0; for(int d=-RAD;d<=RAD;d++) bs=std::min(bs,miss(d,0)); freeSq+=bs;
        int bb=b0; for(int d=-RAD;d<=RAD;d++) for(int e=-2;e<=2;e++) bb=std::min(bb,miss(d,e)); freeBoth+=bb;
      }
      // asymmetry: does +a and -a need DIFFERENT values of the shared square field?
      if(a){ i64 tp=a, tm=-a;
        auto setOf=[&](i64 t,std::vector<char>&ok){ ok.assign(2*RAD+1,1);
            for(int par=0;par<2;par++) for(int n=0;n<17;n++){ int ix=par*17+n;
                i64 idx=((i64)par<<23)+((i64)n<<19)+t; if(idx<((i64)par<<23)||idx>=((i64)(par+1)<<23)) continue;
                u64 q=mant(RZ[idx]), rn=mant(RN[idx]); uint8_t cm=0;
                for(int r=0;r<4;r++){ u64 v=rne2((i64)(4*q+r)); if(v==rn||(rn==0x800000&&v==0x1000000)) cm|=1<<r; }
                if(!cm) continue;
                for(int d=-RAD;d<=RAD;d++){ if(!ok[d+RAD])continue; i64 W=Wof(ix,t,d,0); i64 dd=W-4*(i64)q;
                    if(!(dd>=0&&dd<=3&&((cm>>dd)&1))) ok[d+RAD]=0; } } };
        std::vector<char> op,om; setOf(tp,op); setOf(tm,om);
        bool anyp=false,anym=false,common=false;
        for(int i=0;i<2*RAD+1;i++){ anyp|=op[i]; anym|=om[i]; common|= (op[i]&&om[i]); }
        if(anyp&&anym){ asymT++; if(!common) asym++; } }
    }
    printf("t values sampled %ld (step %d), node-constraints %ld\n",nt,STEP,ns);
    printf("misses, model as published                     : %ld  (%.4f%%)\n",base,100.0*base/ns);
    printf("misses, FREE per-t value of the square field   : %ld  (%.4f%%)\n",freeSq,100.0*freeSq/ns);
    printf("misses, free per-t values of BOTH square fields: %ld  (%.4f%%)\n",freeBoth,100.0*freeBoth/ns);
    printf("|t| where +t and -t need DISJOINT square fields: %ld of %ld comparable\n",asym,asymT);
    return 0; }
