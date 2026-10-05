// rt40_fsrra_hwerr.cpp -- the FSRRA analogue of rt39: model-free per-node error of the hardware against the true 1/sqrt(x),
// in units of 2^-26 of the result mantissa scale.  RZ gives floor(W/4); the RN mantissa refines it; the centre of the
// consistent set is used.  Per node: mean error, error at the centre (|t| < 64), min / max.
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
int main(){ const int NA=1<<24; std::vector<uint32_t> RZ(NA),RN(NA); FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb"); if(!f||fread(RZ.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb"); if(!f||fread(RN.data(),4,NA,f)!=(size_t)NA)return 1; fclose(f);
    puts("par node |   mean     at centre    min     max   | centre x      true C0 (2^26/sqrt x)");
    for(int par=0;par<2;par++) for(int n=0;n<=16;n++){ long long center=((long long)par<<23)+((long long)n<<19); double s=0,sc=0,mn=1e9,mx=-1e9; long cnt=0,cc=0;
        for(long long t=-(1<<18);t<(1<<18);t+=7){ long long idx=center+t; if(idx<((long long)par<<23)||idx>=((long long)(par+1)<<23))continue;
            uint64_t q=(RZ[idx]&0x7fffff)|0x800000, rn=(RN[idx]&0x7fffff)|0x800000; if(((RZ[idx]>>23)&0xff)!=126){ continue; }   // skip the exact 1.0 result
            double lo=1e18,hi=-1e18; for(int r=0;r<4;r++){ uint64_t W=4*q+r; uint64_t qq=W>>2; int rr=W&3; uint64_t v=qq+((rr>2)||(rr==2&&(qq&1))); if(v==rn||(rn==0x800000&&v==0x1000000)){ if(W<lo)lo=W; if(W>hi)hi=W; } } if(lo>hi)continue;
            long double x=(par?2.0L:1.0L)*(1.0L+(long double)(idx-((long long)par<<23))/8388608.0L); long double tr=67108864.0L/sqrtl(x); double e=0.5*(lo+hi)-(double)tr;
            s+=e; cnt++; if(e<mn)mn=e; if(e>mx)mx=e; if(llabs(t)<64){sc+=e;cc++;} }
        long double xc=(par?2.0L:1.0L)*(1.0L+(long double)n/16.0L); if(cnt) printf(" %d   %2d   | %+6.2f    %+6.2f    %+6.2f  %+6.2f  |  %.4Lf     %.3Lf\n",par,n,s/cnt,cc?sc/cc:0.0,mn,mx,xc,67108864.0L/sqrtl(xc)); }
    return 0; }
