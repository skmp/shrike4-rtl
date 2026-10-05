// we_shape.cpp -- shape of the required truncation c(t) (J = floor((KC t^2 + c)/2^17)).
// If c = (E - r)*t + B with r = (KC t + E) mod W, then c/t lies in (E-W, E], so the spread of the
// required c/t over t reveals W directly.  Prints c/t bounds for the tight t and the global envelope.
#include "we_win.h"
#include <cstring>
#include <algorithm>
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    const i64 M=1ll<<17; int lim=argc>1?atoi(argv[1]):200;
    double gmin=1e18,gmax=-1e18; int gmint=0,gmaxt=0;
    // For t>0 the binding is: c/t must be >= CL/t and <= CH/t.  Collect the max of CL/t and min of CH/t.
    double maxCLt=-1e18,minCHt=1e18; int at1=0,at2=0;
    double maxCLtm=-1e18,minCHtm=1e18; int at3=0,at4=0;
    printf("# t    CL/t      CH/t     (window width in c)/2^17\n");
    for(int t=-512;t<=511;t++){ if(!JOK[t+512]||!t)continue; i64 q=(i64)KC*t*t;
        double CL=(double)(JLo[t+512]*M-q), CH=(double)((JHi[t+512]+1)*M-1-q);
        double a=CL/t, b=CH/t; if(t<0) std::swap(a,b);      // c/t bounds
        if(std::abs(t)>=lim) printf("%5d %10.2f %10.2f   %8.4f\n",t,a,b,(CH-CL)/M);
        if(t>0){ if(a>maxCLt){maxCLt=a;at1=t;} if(b<minCHt){minCHt=b;at2=t;} }
        else   { if(a>maxCLtm){maxCLtm=a;at3=t;} if(b<minCHtm){minCHtm=b;at4=t;} }
        gmin=std::min(gmin,a); gmax=std::max(gmax,b); }
    printf("\nt>0: c/t must be >= %.2f (at t=%d) and <= %.2f (at t=%d)  -> span %.2f\n",maxCLt,at1,minCHt,at2,minCHt-maxCLt);
    printf("t<0: c/t must be >= %.2f (at t=%d) and <= %.2f (at t=%d)  -> span %.2f\n",maxCLtm,at3,minCHtm,at4,minCHtm-maxCLtm);
    printf("global c/t envelope [%.2f,%.2f]\n",gmin,gmax);
    return 0; }
