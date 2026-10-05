// w1_fit.cpp -- THE observable that pins the expansion point.  Fit the hardware's own cubic on each
// node interval (least squares on the dump's window midpoints, long double: a scale estimate only) and
// read its LINEAR coefficient at three candidate expansion points: the interval centre and the two ends.
// A stored "rounded cosine" field is only consistent with the point where the economisation's ODD part
// vanishes.  Difference reported in LSBs of the 23-bit C1 field (field = cos * 6588398 at octave 0).
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <cmath>
static Fsca F; static i64 VL[65536],VH[65536];
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;} }
  printf("Least-squares cubic V(t) = a0 + a1 t + a2 t^2 + a3 t^3 fitted on the node interval, then the\n");
  printf("linear coefficient re-expanded about three points.  Compared with the derived rule\n");
  printf("   C1(A) = cos(2 pi A / 2^16) * 2 pi * 2^10 = cos * 6433.9808  (field LSB = C1/1024)\n\n");
  printf("  node  interval               dC1 at LEFT end   dC1 at CENTRE   dC1 at RIGHT end   (LSBs of C1 field)\n");
  long double sl=0,sc=0,sr=0; int cnt=0;
  for(int n=1;n<=15;n++){
    int c=1024*n;
    long double S[7]={0,0,0,0,0,0,0}, b[4]={0,0,0,0};
    for(int t=-512;t<=511;t++){ long double x=t, y=0.5L*(long double)(VL[c+t]+VH[c+t]);
      long double p=1; for(int k=0;k<7;k++){ S[k]+=p; p*=x; }
      p=1; for(int k=0;k<4;k++){ b[k]+=y*p; p*=x; } }
    long double M[4][5];
    for(int i=0;i<4;i++){ for(int j=0;j<4;j++) M[i][j]=S[i+j]; M[i][4]=b[i]; }
    for(int i=0;i<4;i++){ int pv=i; for(int r=i+1;r<4;r++) if(fabsl(M[r][i])>fabsl(M[pv][i])) pv=r;
      for(int j=0;j<5;j++) std::swap(M[i][j],M[pv][j]);
      for(int r=0;r<4;r++) if(r!=i){ long double f=M[r][i]/M[i][i]; for(int j=i;j<5;j++) M[r][j]-=f*M[i][j]; } }
    long double a[4]; for(int i=0;i<4;i++) a[i]=M[i][4]/M[i][i];
    auto slope=[&](long double t){ return a[1] + 2*a[2]*t + 3*a[3]*t*t; };
    auto truec=[&](int A){ long double ang=(long double)A*2*M_PIl/65536.0L; return cosl(ang)*6433.98083L; };
    long double dL=(slope(-512)-truec(c-512))*1024.0L;
    long double dC=(slope(0)   -truec(c    ))*1024.0L;
    long double dR=(slope(511) -truec(c+511))*1024.0L;
    printf("  %4d  [%6d,%6d]   %+12.2Lf    %+12.2Lf     %+12.2Lf\n",n,c-512,c+511,dL,dC,dR);
    sl+=fabsl(dL); sc+=fabsl(dC); sr+=fabsl(dR); cnt++; }
  printf("  MEAN |deviation|        %12.2Lf    %12.2Lf     %12.2Lf\n",sl/cnt,sc/cnt,sr/cnt);
  printf("\n  -> a rounded cosine field is accurate to +-0.5 LSB.  Only the CENTRE qualifies.\n");
  printf("  -> the predicted end-point shift is 2*sin(A)*q*H^3 * 1024 = %.1f * sin(A) LSBs\n",
         (double)(2.0L*powl(2*M_PIl/65536.0L,4)/24.0L*67108864.0L*512.0L*512.0L*512.0L*1024.0L));
  return 0; }
