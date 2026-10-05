// rr17_refcheck.cpp -- ../fsrra_ref.h against both [1,4) dumps, every input: full RZ words, RN mantissas.
#include <cstdio>
#include <vector>
#include "../fsrra_ref.h"
int main(){ const size_t NA=1<<24; std::vector<uint32_t> RZ(NA),RN(NA);
    FILE*f=fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin","rb"); if(!f||fread(RZ.data(),4,NA,f)!=NA)return 1; fclose(f);
    f=fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin","rb"); if(!f||fread(RN.data(),4,NA,f)!=NA)return 1; fclose(f);
    long brz=0,brn=0; for(uint32_t i=0;i<NA;i++){ uint32_t x=0x3f800000u+i; if(fsrra_ref(x,1)!=RZ[i]){ if(brz<5)printf("RZ x %08x ref %08x hw %08x\n",x,fsrra_ref(x,1),RZ[i]); brz++; } if((fsrra_ref(x,0)&0x7fffff)!=(RN[i]&0x7fffff)){ if(brn<5)printf("RN x %08x ref %08x hw mant %06x\n",x,fsrra_ref(x,0),RN[i]&0x7fffff); brn++; } }
    printf("fsrra_ref.h vs hardware over [1,4): RZ %ld mismatches, RN mantissa %ld mismatches of %zu\n",brz,brn,NA); return brz||brn; }
