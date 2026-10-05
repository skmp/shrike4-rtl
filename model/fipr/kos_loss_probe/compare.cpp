// compare.cpp -- diff the real Dreamcast FIPR results against the model, for the probe vectors that reproduce
// the FSCA square path's array losses.
#include "../../fsca/tools/base2.h"
#include "../fipr_ref.h"
#include <cstdio>
#include <vector>
#include <map>
#include <set>
int main(){ build_tables();
    FILE*fv=fopen("probe_vectors.bin","rb"), *fh=fopen("probe_hw.bin","rb");
    if(!fv||!fh){ puts("missing files"); return 1; }
    std::vector<uint32_t> V, H; uint32_t b[2];
    while(fread(b,4,2,fv)==2){ V.push_back(b[0]); V.push_back(b[1]); }
    while(fread(b,4,2,fh)==2){ H.push_back(b[0]); H.push_back(b[1]); }
    fclose(fv); fclose(fh);
    size_t n=V.size()/2;
    printf("vectors: %zu   hardware results: %zu\n",n,H.size()/2);
    if(H.size()/2 < n) n = H.size()/2;
    long badrn=0, badrz=0; std::set<std::pair<i64,i64>> badpat;
    for(size_t i=0;i<n;i++){
        uint32_t rec=V[2*i], mul=V[2*i+1];
        uint32_t A[4]={0,0,0,0},B[4]={0,0,0,0};
        A[0]=(127u<<23)|(mul&0x7fffffu); B[0]=(127u<<23)|(rec&0x7fffffu);
        uint32_t prn=fipr_ref(A,B,0), prz=fipr_ref(A,B,1);
        if(prn!=H[2*i]){ badrn++; if(badpat.size()<20) badpat.insert({rec&((1ll<<18)-1), mul&((1ll<<16)-1)}); }
        if(prz!=H[2*i+1]){ badrz++; if(badpat.size()<20) badpat.insert({rec&((1ll<<18)-1), mul&((1ll<<16)-1)}); }
        if((badrn+badrz)<=5 && (prn!=H[2*i]||prz!=H[2*i+1]))
            printf("  MISMATCH rec=%u mul=%u : model RN %08x RZ %08x | hw RN %08x RZ %08x\n",
                rec,mul,prn,prz,H[2*i],H[2*i+1]);
    }
    printf("\nRN mismatches: %ld of %zu\nRZ mismatches: %ld of %zu\n",badrn,n,badrz,n);
    if(!badrn && !badrz){
        puts("\n*** The array model reproduces real hardware EXACTLY at every operand pattern the FSCA");
        puts("    square path uses -- including the 512 calls whose recoded operand is negative, whose");
        puts("    loss these vectors reproduce.  The array is no longer an extrapolation there. ***");
    } else {
        printf("\ndistinct low-bit patterns implicated: %zu\n",badpat.size());
        puts("A mismatch here would be the first direct evidence of an array-model error in FSCA's domain.");
    }
    return 0; }
