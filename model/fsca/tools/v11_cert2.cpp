// v11_cert2.cpp -- (1) a CORRECT control for the 4th-difference certificate, (2) the full-phase test of
// "no cubic fits ANY 2048-wide interval", (3) the floor any 32-node partition must pay.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <climits>
#include <cmath>
static Fsca F; static i64 VL[65536],VH[65536],S26[65536];
static i64 cert_equi(const i64*lo,const i64*hi,int base,int len,int smin,int smax){
    static const i64 C4[5]={1,4,6,4,1}; i64 best=LLONG_MIN;
    for(int s=smin;s<=smax&&4*s<=len-1;s++) for(int off=0;off+4*s<=len-1;off++){
        i64 acc=0,wid=0; for(int i=0;i<5;i++){ int k=base+off+i*s; i64 L=lo[k],H=hi[k];
            acc+=(i&1?-1:1)*C4[i]*(L+H); wid+=C4[i]*(H-L); }
        i64 v=(acc<0?-acc:acc)-wid; if(v>best)best=v; }
    return best; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;}
        int q=a>>14,r=a&16383,neg=(q>=2),u=(q&1)?16384-r:r;
        S26[a]=rn_scale(sin_(PI_()*u/32768),26)*(neg?-1:1); }

    printf("=== B'. CORRECT control: an EXACT integer cubic, windows widened by the real dump widths ===\n");
    {   static i64 clo[2048],chi[2048];
        // p(x) = a0 + a1 x + a2 x^2 + a3 x^3 with integer coefficients -> p(x) is an exact integer
        i64 a0=67000000,a1=311,a2=-13,a3=7;
        for(int i=0;i<2048;i++){ i64 x=i-1024; i64 p=a0+x*(a1+x*(a2+x*a3));
            i64 w=VH[14336-1024+i]-VL[14336-1024+i];   // real dump window widths
            clo[i]=p-w/2; chi[i]=clo[i]+w; }           // window CONTAINS the exact cubic value
        i64 r=cert_equi(clo,chi,0,2048,1,511);
        printf("  certificate on an exact cubic = %.4f  (must be <= 0)   %s\n",(double)r/32.0, r<=0?"OK":"*** FAIL ***");
        // second control: exact cubic + a bounded 'truncation' of at most T units, window width 0
        for(int T : {1,3,8,25}){
            for(int i=0;i<2048;i++){ i64 x=i-1024; i64 p=a0+x*(a1+x*(a2+x*a3));
                i64 e = ((i*2654435761u)>>28)%(T+1);   // deterministic pseudo-random 0..T
                clo[i]=chi[i]=p-e; }
            i64 r2=cert_equi(clo,chi,0,2048,1,511);
            printf("  exact cubic minus truncation in [0,%2d], zero-width windows -> certificate %8.4f  (bound says <= %d)  %s\n",
                   T,(double)r2/32.0,T, (double)r2/32.0<=T+1e-9?"OK":"*** FAIL ***"); } }

    printf("\n=== E'. every phase: the 2048-wide interval certificate as a function of its centre ===\n");
    printf("  (step 16 over a full half-turn; a cubic 'fits' only where this is small)\n");
    {   i64 mn=LLONG_MAX; int mnc=0; i64 mx=LLONG_MIN; int mxc=0;
        static i64 buf[70000];
        printf("   centre :  cert   |");
        int col=0;
        for(int c=0;c<32768;c+=16){
            static i64 lo[2048],hi[2048];
            for(int i=0;i<2048;i++){ int a=(c-1024+i)&0xffff; lo[i]=VL[a]; hi[i]=VH[a]; }
            i64 v=cert_equi(lo,hi,0,2048,500,511);   // restricted spacings: still a valid LOWER bound
            if(v<mn){mn=v;mnc=c;} if(v>mx){mx=v;mxc=c;}
            if(c%2048==0){ printf(" %5d:%7.2f",c,(double)v/32.0); if(++col%5==0)printf("\n                   |"); }
            (void)buf; }
        printf("\n  MIN over all centres (step 16): %.3f at centre %d\n",(double)mn/32.0,mnc);
        printf("  MAX over all centres (step 16): %.3f at centre %d\n",(double)mx/32.0,mxc); }

    printf("\n=== E''. the FLOOR any 32-node partition must pay ===\n");
    printf("  Every 2048-wide partition has one interval whose centre lies within 1024 of angle 16384.\n");
    {   i64 mn=LLONG_MAX; int mnc=0;
        for(int c=16384-1024;c<=16384+1024;c++){
            static i64 lo[2048],hi[2048];
            for(int i=0;i<2048;i++){ int a=(c-1024+i)&0xffff; lo[i]=VL[a]; hi[i]=VH[a]; }
            i64 v=cert_equi(lo,hi,0,2048,505,511);
            if(v<mn){mn=v;mnc=c;} }
        printf("  min over the 2049 possible such centres: %.3f (at centre %d)\n",(double)mn/32.0,mnc);
        printf("  -> NO 32-node partition, at ANY phase, admits a per-node cubic closer than that to the dump.\n"); }

    printf("\n=== the same floor for a 64-node partition, for comparison ===\n");
    {   i64 mn=LLONG_MAX; int mnc=0;
        for(int c=16384-512;c<=16384+512;c++){
            static i64 lo[1024],hi[1024];
            for(int i=0;i<1024;i++){ int a=(c-512+i)&0xffff; lo[i]=VL[a]; hi[i]=VH[a]; }
            i64 v=cert_equi(lo,hi,0,1024,1,255);
            if(v<mn){mn=v;mnc=c;} }
        printf("  min over the 1025 possible centres near 16384: %.3f (at centre %d)\n",(double)mn/32.0,mnc); }
    return 0; }
