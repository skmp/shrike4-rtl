// rt39_hwerr.cpp -- MODEL-FREE: the hardware's approximation error against the true sine, per node.
// err = V_hw (window centre) - sin*2^26, over the 1024 offsets of node n and of its mirror 32-n.
// A degree-3 minimax approximation has err(0) = -A, err(+-h/sqrt2) = +A, err(+-h) = -A with A = 2.03*sin units, plus
// truncation biases.  Columns: mean, min, max, and the error at the three Chebyshev stations (averaged over +-t, n, 32-n).
#include "base2.h"
#include <cmath>
static Fsca F;
int main(){ if(!load_fsca(F))return 1; long double PI=acosl(-1.0L);
    puts("node  e1  C0-RN(econ) |   mean     min     max  |  err(t=0)  err(|t|~362)  err(|t|~512)  |  2.03*sin  | mean over |t|<=64   mean over 300<=|t|<=420");
    int dev[17]={0,0,1,0,0,0,0,0,0,-1,3,3,0,3,-2,1,1}; int e1[17]={0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3,0};
    for(int n=1;n<=16;n++){ double s=0,mn=1e9,mx=-1e9; int cnt=0; double e0=0,e362=0,e512=0; int c0=0,c362=0,c512=0; double sm_=0; int csm=0; double lg=0; int clg=0;
        for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); double v=0.5*(w.lo+w.hi)*w.sign; double tr=(double)(sinl(2*PI*a/65536.0L)*67108864.0L); double e=v-tr;
                s+=e; cnt++; if(e<mn)mn=e; if(e>mx)mx=e; int at=abs(t); if(at<=8){e0+=e;c0++;} if(at>=354&&at<=370){e362+=e;c362++;} if(at>=500){e512+=e;c512++;} if(at<=64){sm_+=e;csm++;} if(at>=300&&at<=420){lg+=e;clg++;} } }
        printf(" %2d   %2d     %+d       | %+6.2f  %+6.2f  %+6.2f |  %+6.2f     %+6.2f        %+6.2f      |   %5.2f    |     %+6.2f              %+6.2f\n",n,e1[n],dev[n],s/cnt,mn,mx,e0/c0,e362/c362,e512/c512,2.029*sin(M_PI*n/32),sm_/csm,lg/clg); }
    return 0; }
