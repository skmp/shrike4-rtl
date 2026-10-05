// r1_rmode.cpp -- differential analysis over the ROUNDING MODES.
// The (RN,RZ) pair pins V to a window. Pass/fail uses only "is V in it"; the POSITION of V inside the
// window is extra sub-LSB information available at all 65536 angles, not just the 294 failures.
#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <cmath>
static Fsca F;
int main(){ if(!load_fsca(F)) return 1;
    std::map<int,int> wid; long long n=0;
    double sd=0, sd2=0; long long nin=0;
    // per-band bias
    double bt[8]={0}; int nt[8]={0};
    double bn[17]={0}; int nn[17]={0};
    double bpos=0,bneg=0; int npos=0,nneg=0;
    int cRN=0,cRZ=0,cBOTH=0;
    std::map<int,int> wBOTH,wRN,wRZ,wOK;
    for(int a=0;a<65536;a++){
        uint32_t hrn=F.srn[a],hrz=F.srz[a];
        Win w=decode(hrz,hrn); int64_t lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        int64_t V=fsca2_internal(a);
        int W=(int)(hi-lo+1); wid[W]++; n++;
        bool rnw=(hrn!=fsca2_sin(a,0)), rzw=(hrz!=fsca2_sin(a,1));
        if(rnw&&rzw){cBOTH++;wBOTH[W]++;} else if(rnw){cRN++;wRN[W]++;} else if(rzw){cRZ++;wRZ[W]++;} else wOK[W]++;
        if(V>=lo&&V<=hi){
            double c=(double)(lo+hi)/2.0, d=(double)V-c;      // signed offset from window centre
            sd+=d; sd2+=d*d; nin++;
            int q=(int)((a+512u)>>10), m=q&63, t=a-1024*q; if(t<-512)t+=65536;
            if(m>=33)m-=32; if(m>=17)m=32-m;
            int b=(t<0?-t:t)/64; if(b>7)b=7; bt[b]+=d; nt[b]++;
            if(m>=0&&m<17){bn[m]+=d;nn[m]++;}
            if(t>0){bpos+=d;npos++;} else if(t<0){bneg+=d;nneg++;}
        }
    }
    printf("window width distribution over all %lld angles:\n  ",n);
    for(auto&p:wid) printf("w=%d:%d  ",p.first,p.second); printf("\n\n");
    printf("failure class by window width (the differential you asked for):\n");
    printf("  width :"); for(auto&p:wid) printf(" %6d",p.first); printf("\n");
    printf("  clean :"); for(auto&p:wid) printf(" %6d",wOK[p.first]); printf("\n");
    printf("  RN only:"); for(auto&p:wid) printf(" %6d",wRN[p.first]); printf("\n");
    printf("  RZ only:"); for(auto&p:wid) printf(" %6d",wRZ[p.first]); printf("\n");
    printf("  BOTH   :"); for(auto&p:wid) printf(" %6d",wBOTH[p.first]); printf("\n");
    printf("  totals: RN-only %d, RZ-only %d, BOTH %d\n\n",cRN,cRZ,cBOTH);
    printf("POSITION OF THE MODEL INSIDE ITS WINDOW (grid units from centre), over the %lld angles that pass:\n",nin);
    printf("  mean %+.4f   rms %.4f   (0 = unbiased)\n",sd/nin,sqrt(sd2/nin));
    printf("  by |t| band:"); for(int b=0;b<8;b++) printf(" %d-%d:%+.3f",b*64,b*64+63,nt[b]?bt[b]/nt[b]:0); printf("\n");
    printf("  t>0 %+.4f (n=%d)   t<0 %+.4f (n=%d)\n",npos?bpos/npos:0,npos,nneg?bneg/nneg:0,nneg);
    printf("  by node:"); for(int m=0;m<17;m++) printf(" %d:%+.3f",m,nn[m]?bn[m]/nn[m]:0); printf("\n");
    return 0; }
