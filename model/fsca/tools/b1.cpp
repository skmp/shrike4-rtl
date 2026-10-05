// b1.cpp -- which symmetries does the hardware satisfy bit-exactly? (decides the fundamental domain)
#include "base2.h"
static Fsca F;
int main(){ if(!load_fsca(F)){puts("no dump");return 1;}
    int m_quarter=0,m_half=0,m_mirror=0,m_odd=0; int ex_mirror[8]={0},nm=0;
    for(int a=0;a<65536;a++){
        int b=(a+0x4000)&0xffff; if(F.crn[a]!=F.srn[b]||F.crz[a]!=F.srz[b]) m_quarter++;
        b=(a+0x8000)&0xffff; if((F.srn[a]^0x80000000u)!=F.srn[b]||(F.srz[a]^0x80000000u)!=F.srz[b]) m_half++;
        b=(0x8000-a)&0xffff; if(F.srn[a]!=F.srn[b]||F.srz[a]!=F.srz[b]){ m_mirror++; if(nm<8) ex_mirror[nm++]=a; }
        b=(-a)&0xffff; if((F.srn[a]^0x80000000u)!=F.srn[b]||(F.srz[a]^0x80000000u)!=F.srz[b]) m_odd++;
    }
    printf("cos=sin(a+0x4000): %d mismatches\n", m_quarter);
    printf("sin(a+0x8000)=-sin(a) (bitwise sign flip): %d\n", m_half);
    printf("sin(0x8000-a)=sin(a) (mirror about pi/2): %d\n", m_mirror);
    printf("sin(-a)=-sin(a): %d\n", m_odd);
    if(m_mirror){ printf("first mirror mismatches:"); for(int i=0;i<nm;i++){int a=ex_mirror[i];printf(" a=%d(%08x/%08x vs %08x/%08x)",a,F.srn[a],F.srz[a],F.srn[(0x8000-a)&0xffff],F.srz[(0x8000-a)&0xffff]);} printf("\n"); }
    return 0; }
