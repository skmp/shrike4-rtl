// v10_fit.cpp -- INDEPENDENT test of "where is the polynomial expanded?", i.e. the user's reading
// that t is the distance from the LAST LUT ENTRY POINT rather than from the node CENTRE.
// Model-free: uses only the decoded dump windows.  Moment sums are EXACT (__int128); only the final
// solve of the 2x2 normal equations is long double, and each is reported with its residual scale.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cmath>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
typedef __int128 i128;
static long double LD(i128 v){ return (long double)(int64_t)(v>>64)*18446744073709551616.0L + (long double)(uint64_t)v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
  const long double TWOPI = 6.283185307179586476925286766559L;
  const long double C1SCALE = TWOPI/65536.0L*67108864.0L;      // output units per t at cos=1
  printf("=== v10_fit: expansion point, from the dump alone ===\n");
  printf("C1 slope scale = 2*pi/2^16 * 2^26 = %.4Lf output units per t\n\n",C1SCALE);
  // For each node 1..15 fit V(t) = c0+c1 t+c2 t^2+c3 t^3 by least squares on the SYMMETRIC set
  // t in [-511,511] (odd/even decouple exactly). Data = window midpoint, 2*V used to stay integral.
  i128 St2=0,St4=0,St6=0; int N=0;
  for(int t=-511;t<=511;t++){ i128 x=t; St2+=x*x; St4+=x*x*x*x; St6+=x*x*x*x*x*x; N++; }
  long double det_o = LD(St2)*LD(St6)-LD(St4)*LD(St4);
  long double det_e = (long double)N*LD(St4)-LD(St2)*LD(St2);
  printf("node  A      cos(A)     |  dev at CENTRE   dev at LEFT     dev at RIGHT   (LSBs of the 23-bit C1 field)\n");
  long double mc=0,ml=0,mr=0; int cnt=0;
  for(int n=1;n<=15;n++){
    i128 S0=0,S1=0,S2=0,S3=0;
    for(int t=-511;t<=511;t++){ int a=(n*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
      i128 v2=(i128)(lo+hi);                    // 2*V, integral
      i128 x=t; S0+=v2; S1+=x*v2; S2+=x*x*v2; S3+=x*x*x*v2; }
    long double c1=(LD(St6)*LD(S1)-LD(St4)*LD(S3))/det_o/2.0L;   // /2 undoes the 2*V
    long double c3=((long double)LD(St2)*LD(S3)-LD(St4)*LD(S1))/det_o/2.0L;
    long double c2=((long double)N*LD(S2)-LD(St2)*LD(S0))/det_e/2.0L;
    // LSB of the 23-bit C1 field, in output units per t: 2^(e1-10)
    long double lsb = powl(2.0L,(long double)(TB[n].e1-10));
    long double res[3];
    for(int k=0;k<3;k++){ long double h = (k==0)?0.0L : (k==1? -512.0L : 512.0L);
      long double slope = c1 + 2.0L*c2*h + 3.0L*c3*h*h;         // p'(h)
      long double A = (long double)(n*1024) + h;                // angle at the expansion point
      long double want = cosl(TWOPI*A/65536.0L)*C1SCALE;        // the derived rule there
      res[k] = (slope-want)/lsb; }
    long double A=(long double)n*1024.0L;
    printf("%4d %6.0Lf  %.6Lf  | %+13.2Lf %+14.2Lf %+14.2Lf   (predicted at an end: -/+ %.1Lf = 2*q*H^3/LSB)\n",
      n,A,cosl(TWOPI*A/65536.0L),res[0],res[1],res[2],
      2.0L*sinl(TWOPI*A/65536.0L)*(powl(TWOPI/65536.0L,4.0L)/24.0L*67108864.0L)*powl(512.0L,3.0L)/lsb);
    mc+=fabsl(res[0]); ml+=fabsl(res[1]); mr+=fabsl(res[2]); cnt++;
  }
  printf("\nMEAN |deviation| over the 15 interior nodes:  centre %.2Lf LSB   left end %.2Lf LSB   right end %.2Lf LSB\n",mc/cnt,ml/cnt,mr/cnt);
  puts("A stored rounded cosine can only be within +-0.5 LSB of the true cosine at its own angle.");

  // ---- period / phase of the residual, model-free: E(a) = V_hw(a) - RN(sin*2^26)
  puts("\n=== residual period test: is the node pitch 1024 (centred) or 512 (start-aligned)? ===");
  {  long double sum_at[4]={0,0,0,0}; int cn[4]={0,0,0,0};
     // bucket by a mod 1024 in four classes: 0, 256, 512, 768 (exact multiples only)
     for(int a=1024;a<16384;a+=256){ i64 lo,hi; win_of(a,lo,hi);
        long double vh=(long double)(lo+hi)/2.0L;
        i128 num=(i128)0; // exact RN(sin*2^26) via the Q60 integer sine already in base2.h is per-node only;
        long double ref = sinl(TWOPI*(long double)a/65536.0L)*67108864.0L;
        long double e = vh-ref; int c=(a%1024)/256; sum_at[c]+=e; cn[c]++; }
     for(int c=0;c<4;c++) printf("  a mod 1024 == %4d : mean E = %+8.3Lf over %d angles\n",c*256,sum_at[c]/cn[c],cn[c]);
     puts("  (period 1024 <=> classes 0 and 512 differ; period 512 <=> they agree)");
  }
  // ---- the equioscillation signature inside a node
  puts("\n=== E(t) averaged over nodes 1..15, at the Chebyshev-significant offsets ===");
  for(int t : {-512,-362,-342,0,342,362,512}){ long double s=0; int c=0;
     for(int n=1;n<=15;n++){ int a=(n*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
        long double vh=(long double)(lo+hi)/2.0L;
        long double ref=sinl(TWOPI*(long double)a/65536.0L)*67108864.0L; s+=vh-ref; c++; }
     printf("  t=%+5d : mean E = %+8.3Lf\n",t,s/c); }
  printf("  quartic-remainder amplitude prediction q*H^4/8*2 at H=512: %.3Lf*sin(A) ; at H=256: %.4Lf ; at H=1024: %.2Lf\n",
     (powl(TWOPI/65536.0L,4.0L)/24.0L*67108864.0L)*powl(512.0L,4.0L)/8.0L*2.0L,
     (powl(TWOPI/65536.0L,4.0L)/24.0L*67108864.0L)*powl(256.0L,4.0L)/8.0L*2.0L,
     (powl(TWOPI/65536.0L,4.0L)/24.0L*67108864.0L)*powl(1024.0L,4.0L)/8.0L*2.0L);
  return 0; }
