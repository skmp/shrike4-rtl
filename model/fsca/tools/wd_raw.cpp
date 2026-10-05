// wd_raw.cpp -- raw dump audit at the t=+-17 conflict samples: print the RN/RZ floats, the decoded
// window, and the mirror-sum/difference constraints that pin T1 and (C0 - T2) without any model.
#include "base2.h"
#include <cstring>
static Fsca F;
static void one(int node,int t){ int a=(node*1024+t)&0xffff;
    uint32_t rn=F.srn[a],rz=F.srz[a]; Win w=decode(rz,rn);
    printf("  node%2d t=%+4d angle=%5d  RN=%08x (%.10f)  RZ=%08x (%.10f)  win[%lld,%lld]%s\n",
        node,t,a,rn,(double)*(float*)&rn,rz,(double)*(float*)&rz,(long long)w.lo,(long long)w.hi,w.exact?"*":""); }
static void pair(int n,int t){ int m=32-n;
    printf("table %d  t=%+d\n",n,t); one(n,t); one(m,t);
    int a1=(n*1024+t)&0xffff, a2=(m*1024+t)&0xffff; Win w1=decode(F.srz[a1],F.srn[a1]),w2=decode(F.srz[a2],F.srn[a2]);
    i64 slo=w1.lo+w2.lo, shi=w1.hi+w2.hi, dlo=w1.lo-w2.hi, dhi=w1.hi-w2.lo;
    printf("    sum V(n)+V(32-n)=2*(C0-T2) in [%lld,%lld] -> C0-T2 in {",(long long)slo,(long long)shi);
    for(i64 s=slo;s<=shi;s++) if(!(s&1)) printf("%lld ",(long long)(s/2)); printf("}\n");
    printf("    dif V(n)-V(32-n)=2*T1        in [%lld,%lld] -> T1 in {",(long long)dlo,(long long)dhi);
    for(i64 s=dlo;s<=dhi;s++) if(!(s&1)) printf("%lld ",(long long)(s/2)); printf("}\n"); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t : {17,-17}) { pair(5,t); pair(8,t); }
    puts("\n-- t=0 entries of the two tables --");
    for(int n : {5,8}) { one(n,0); one(32-n,0); }
    puts("\n-- do the two mirror samples ever carry an ODD sum (would break C0+/-T1-T2 exactly)? --");
    int odd=0,amb=0;
    for(int n=1;n<=15;n++) for(int t=-512;t<=511;t++){ int a1=(n*1024+t)&0xffff,a2=((32-n)*1024+t)&0xffff;
        Win w1=decode(F.srz[a1],F.srn[a1]),w2=decode(F.srz[a2],F.srn[a2]);
        i64 slo=w1.lo+w2.lo,shi=w1.hi+w2.hi; int ne=0; for(i64 s=slo;s<=shi;s++) if(!(s&1))ne++;
        if(ne==0)odd++; if(ne>1)amb++; }
    printf("mirror pairs with NO even sum (impossible under the model): %d ; with >1: %d ; total 15360\n",odd,amb);
    return 0; }
