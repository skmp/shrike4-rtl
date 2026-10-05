// gen_vectors.cpp -- build the hardware probe vector file and the model's prediction for it.
// Purpose: the FSCA square path uses the FIPR array with operands FIPR itself never presents (smaller than
// 2^23, and sometimes negative).  But arr(rec,mul) = rec*mul + loss(rec mod 2^18, mul mod 2^16) exactly --
// verified in all four sign quadrants -- and a FIPR mantissa may carry ANY low bits, only bit 23 is forced.
// So a FIPR call with the same low bits exercises the IDENTICAL loss, and the array model can be validated on
// real hardware at exactly the bit patterns FSCA produces.
#include "../../fsca/tools/base2.h"
#include "../fipr_ref.h"
#include <cstdio>
#include <set>
#include <vector>
#include <algorithm>
int main(){ build_tables();
    std::set<std::pair<i64,i64>> pat;                       // the low-bit patterns FSCA's square path uses
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
        i64 rr[3]={KC,I1,J}, mm[3]={dx,dx,TB[8].S};
        for(int i=0;i<3;i++) pat.insert({ ((rr[i]%(1ll<<18))+(1ll<<18))%(1ll<<18),
                                          ((mm[i]%(1ll<<16))+(1ll<<16))%(1ll<<16) }); }
    std::vector<std::pair<uint32_t,uint32_t>> vec;          // (rec mantissa, mul mantissa), both 24-bit
    for(auto&p:pat){ i64 rlo=p.first, mlo=p.second;
        // several high-bit variants per pattern: the loss is unchanged, the product moves across the
        // rounding boundary, so the set of FIPR answers localises the loss
        int kept=0;
        for(i64 rh=(1ll<<5); rh<(1ll<<6) && kept<16; rh+=3)
          for(i64 mh=(1ll<<7); mh<(1ll<<8) && kept<16; mh+=23){
            i64 rec=(rh<<18)|rlo, mul=(mh<<16)|mlo;
            if(rec<(1ll<<23)||rec>=(1ll<<24)||mul<(1ll<<23)||mul>=(1ll<<24)) continue;
            vec.push_back({(uint32_t)rec,(uint32_t)mul}); kept++; } }
    printf("patterns %zu, vectors %zu\n",pat.size(),vec.size());
    FILE*f=fopen("probe_vectors.bin","wb");
    for(auto&v:vec){ uint32_t a[2]={v.first,v.second}; fwrite(a,4,2,f); } fclose(f);
    // the model's prediction, so the hardware result can be diffed directly
    FILE*g=fopen("probe_predicted.bin","wb");
    long checked=0;
    for(auto&v:vec){ uint32_t A[4]={0,0,0,0},B[4]={0,0,0,0};
        A[0]=(127u<<23)|(v.second & 0x7fffffu);             // FVm = multiplicand
        B[0]=(127u<<23)|(v.first  & 0x7fffffu);             // FVn = recoded operand
        uint32_t rn=fipr_ref(A,B,0), rz=fipr_ref(A,B,1);
        uint32_t o[2]={rn,rz}; fwrite(o,4,2,g); checked++; }
    fclose(g);
    printf("wrote probe_vectors.bin (%zu x 8 B) and probe_predicted.bin\n",vec.size());
    // sanity: the loss of each twin equals the loss of the FSCA call it stands for
    int bad=0,n=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(dx,KC)+K)>>23;
        i64 rp=(1ll<<23)|(((I1%(1ll<<18))+(1ll<<18))%(1ll<<18));
        i64 mp=(1ll<<23)|(((dx%(1ll<<16))+(1ll<<16))%(1ll<<16));
        if((arr(I1,dx)-I1*dx) != (arr(rp,mp)-rp*mp)) bad++; n++; }
    printf("twin-loss check on the second square multiply: %d mismatches of %d\n",bad,n);
    return 0; }
