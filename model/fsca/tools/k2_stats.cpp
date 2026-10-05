#include "../archive/fsca_ref2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
static Fsca F;
static void nodeof(int a,int&m,int&t){ int q=(a+512)>>10; m=q&63; t=a-1024*q; if(t<-512)t+=65536; }
int main(){ if(!load_fsca(F)) return 1;
    std::map<int,int> dd, ulpd, absT; int nlow=0,nhigh=0;
    std::vector<int> bad;
    for(int a=0;a<65536;a++){
        uint32_t hrn=F.srn[a],hrz=F.srz[a],mrn=fsca2_sin(a,0),mrz=fsca2_sin(a,1);
        if(hrn==mrn&&hrz==mrz) continue;
        bad.push_back(a);
        int m,t; nodeof(a,m,t); absT[t<0?-t:t]++;
        Win w=decode(hrz,hrn); i64 lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        i64 V=fsca2_internal(a); i64 d = V<lo ? V-lo : V-hi; dd[(int)d]++;
        if(V<lo) nlow++; else nhigh++;
        // ulp delta on whichever rounding differs
        if(hrn!=mrn) ulpd[(int)((int64_t)(mrn&0x7fffffff)-(int64_t)(hrn&0x7fffffff))]++;
        if(hrz!=mrz) ulpd[(int)((int64_t)(mrz&0x7fffffff)-(int64_t)(hrz&0x7fffffff))]++;
    }
    printf("internal error (model - nearest edge of hw window), 2^-26 units:\n  ");
    for(auto&p:dd) printf("%+d: %d   ",p.first,p.second); printf("\n  model too LOW %d, too HIGH %d\n\n",nlow,nhigh);
    printf("float error (model bits - hw bits), i.e. ulps of the result:\n  ");
    for(auto&p:ulpd) printf("%+d ulp: %d   ",p.first,p.second); printf("\n\n");
    printf("distribution over |t| (offset from node centre, max 512):\n  ");
    int lo128=0,mid=0,hi384=0; for(auto&p:absT){ if(p.first<128)lo128+=p.second; else if(p.first<384)mid+=p.second; else hi384+=p.second; }
    printf("|t|<128: %d    128<=|t|<384: %d    |t|>=384: %d\n",lo128,mid,hi384);
    printf("  smallest |t| affected: %d      largest: %d\n", absT.begin()->first, absT.rbegin()->first);
    // node 0,1,2,16 check
    printf("\nnodes with ZERO mismatches: ");
    int pn[64]; memset(pn,0,sizeof pn); for(int a:bad){int m,t;nodeof(a,m,t);pn[m]++;}
    for(int m=0;m<64;m++) if(!pn[m]) printf("%d ",m); printf("\n");
    return 0; }
