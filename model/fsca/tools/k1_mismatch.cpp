// k1_mismatch.cpp -- every angle where the delivered structural model (../fsca_ref2.h) disagrees with the
// hardware, with the input and both outputs in binary, hex and decimal, plus the adjacent angles for context.
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
static Fsca F;
static void b16(unsigned v,char*o){ for(int i=15;i>=0;i--) *o++ = ((v>>i)&1)?'1':'0'; *o=0; }
static void b32(uint32_t v,char*o){ for(int i=31;i>=0;i--){ *o++ = ((v>>i)&1)?'1':'0'; if(i==31||i==23) *o++='.'; } *o=0; }
static double f32(uint32_t b){ float f; memcpy(&f,&b,4); return (double)f; }
static void nodeof(int a,int&m,int&t){ int q=(a+512)>>10; m=q&63; t=a-1024*q; if(t<-512)t+=65536; }
int main(){ if(!load_fsca(F)) return 1;
    std::vector<int> bad;
    for(int a=0;a<65536;a++){ if(fsca2_sin(a,0)!=F.srn[a] || fsca2_sin(a,1)!=F.srz[a]) bad.push_back(a); }
    // summary
    int pernode[64]; memset(pernode,0,sizeof pernode); int rn=0,rz=0,both=0;
    for(int a : bad){ int m,t; nodeof(a,m,t); pernode[m]++;
        bool r=fsca2_sin(a,0)!=F.srn[a], z=fsca2_sin(a,1)!=F.srz[a];
        rn+=r; rz+=z; both+=(r&&z); }
    printf("MISMATCHING ANGLES: %zu distinct (of 65536)   RN %d, RZ %d, both %d\n",bad.size(),rn,rz,both);
    printf("per node:"); for(int m=0;m<64;m++) if(pernode[m]) printf(" %d:%d",m,pernode[m]); printf("\n");
    // how isolated are they?
    int adj=0; for(size_t i=1;i<bad.size();i++) if(bad[i]==bad[i-1]+1) adj++;
    printf("adjacent pairs among them: %d  (so they are %s)\n\n",adj, adj? "sometimes clustered":"all isolated");
    FILE* f=fopen("k_mismatches.txt","w");
    char B[80],C[80];
    auto dump=[&](FILE*o,int a,const char* tag){
        int m,t; nodeof(a,m,t);
        uint32_t hrn=F.srn[a],hrz=F.srz[a],mrn=fsca2_sin(a,0),mrz=fsca2_sin(a,1);
        b16((unsigned)a,B);
        fprintf(o,"%s angle  0x%04X  0b%s   node %2d  t = %+5d%s\n",tag,a,B,m,t,
            (hrn==mrn&&hrz==mrz)?"   (model matches)":"   <== MODEL DIFFERS");
        b32(hrn,B); b32(mrn,C);
        fprintf(o,"    RN  hw    0x%08X  0b%s  %.9g\n",hrn,B,f32(hrn));
        fprintf(o,"        model 0x%08X  0b%s  %.9g%s\n",mrn,C,f32(mrn), hrn!=mrn?"   <-- differs":"");
        b32(hrz,B); b32(mrz,C);
        fprintf(o,"    RZ  hw    0x%08X  0b%s  %.9g\n",hrz,B,f32(hrz));
        fprintf(o,"        model 0x%08X  0b%s  %.9g%s\n",mrz,C,f32(mrz), hrz!=mrz?"   <-- differs":"");
        Win w=decode(hrz,hrn); i64 lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        i64 V=fsca2_internal(a);
        i64 d = V<lo ? V-lo : (V>hi ? V-hi : 0);
        fprintf(o,"    internal (2^-26 units)  hw in [%lld, %lld]%s   model %lld   -> %s%+lld\n",
            (long long)lo,(long long)hi,w.exact?" exact":"",(long long)V,
            d? "OUTSIDE by ":"inside, ", (long long)d);
    };
    fprintf(f,"FSCA: every angle where the structural model ../fsca_ref2.h disagrees with the hardware dump.\n");
    fprintf(f,"Input is FPUL & 0xffff (one turn = 65536).  Outputs are the raw float32 bits of the sine result.\n");
    fprintf(f,"Binary float shown as  sign.exponent.mantissa.  'internal' is the value on the 2^-26 grid that\n");
    fprintf(f,"the hardware's RN/RZ pair pins, against what the model computes.\n");
    fprintf(f,"%zu mismatching angles.\n",bad.size());
    for(int a : bad){
        fprintf(f,"\n================================================================================\n");
        dump(f,(a-1)&0xffff,"PREV");
        fprintf(f,"\n"); dump(f,a,"**** ");
        fprintf(f,"\n"); dump(f,(a+1)&0xffff,"NEXT");
    }
    fclose(f);
    printf("wrote k_mismatches.txt (%zu entries)\n\nFIRST THREE, as they appear in the file:\n",bad.size());
    for(int i=0;i<3&&i<(int)bad.size();i++){ int a=bad[i];
        printf("--------------------------------------------------------------------------------\n");
        dump(stdout,(a-1)&0xffff,"PREV"); printf("\n"); dump(stdout,a,"**** "); printf("\n"); dump(stdout,(a+1)&0xffff,"NEXT"); }
    return 0; }
