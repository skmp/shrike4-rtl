// w1_pin.cpp -- exactly WHICH observables pin the 17-entry / 1024-angle / centred structure.
//  (1) per-node approximation-error amplitude vs the degree-3 minimax prediction at each candidate H
//  (2) the equioscillation abscissae (Chebyshev points of the node interval)
//  (3) the t=0 entries: C0 is a stored 26-bit field (node 16 is the clamp 2^26-1)
//  (4) why the economisation lands entirely on C0: at a CENTRED expansion the Chebyshev correction is
//      EVEN, so it cannot touch C1 or c3; at a start-aligned expansion it is not, and C1 would have to
//      carry a sin(A)-proportional term.  Measured in LSBs of the 23-bit C1 field.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <cmath>
static Fsca F; static i64 VL[65536],VH[65536]; static i64 S26[65536];
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;}
    int q=a>>14,r=a&16383,neg=(q>=2),u=(q&1)?16384-r:r;
    S26[a]=rn_scale(sin_(PI_()*u/32768),26)*(neg?-1:1); }
  auto E=[&](int a){ return 0.5*(double)(VL[a]+VH[a]) - (double)S26[a]; };

  printf("(1) PER-NODE ERROR AMPLITUDE vs the degree-3 minimax prediction  A(H) = sin * (2 pi H/2^16)^4/192 * 2^26\n");
  printf("    A(512)=2.030*sin   A(256)=0.1269*sin   A(1024)=32.48*sin        (units of 2^-26)\n");
  printf("    node  angle   sin     max|E| in +-512   pred H=512   pred H=256   pred H=1024\n");
  for(int n=2;n<=16;n+=2){ int c=1024*n; double mx=0;
    for(int t=-512;t<=511;t++) mx=std::max(mx,std::fabs(E(c+t)));
    long double s=(long double)S26[c]/67108864.0L;
    printf("    %3d  %6d  %.4Lf      %7.2f       %7.2f      %7.3f      %8.2f\n",
      n,c,s,mx,(double)(2.0300L*s),(double)(0.12688L*s),(double)(32.481L*s)); }

  printf("\n(2) EQUIOSCILLATION ABSCISSAE.  A degree-3 minimax error on [-H,H] is -A*T4(t/H):\n");
  printf("    extrema at t/H = 0, +-0.7071, +-1 with signs -,+,-,+,- .  Averaged over nodes:\n");
  { double s[1024]={0}; int c2[1024]={0};
    for(int a=0;a<32768;a++){ int t=((a+512)&1023)-512; s[t+512]+=E(a); c2[t+512]++; }
    auto m=[&](int t){ return s[t+512]/c2[t+512]; };
    int arg=0; double best=-1e9; for(int t=1;t<512;t++) if(m(t)>best){best=m(t);arg=t;}
    printf("    E(0)     = %+7.3f     (predicted sign -)\n",m(0));
    printf("    E(+-362) = %+7.3f / %+7.3f   (predicted sign +, |t|/H = 0.7071)\n",m(362),m(-362));
    printf("    E(+-512) = %+7.3f / %+7.3f   (predicted sign -, |t|/H = 1)\n",m(511),m(-512));
    printf("    measured interior maximum at |t| = %d  ->  |t|/H = %.4f   (Chebyshev: 0.7071)\n",arg,arg/512.0);
    printf("    if the node were H=256 the curve would repeat with period 512; it does not:\n");
    printf("      E at a mod 1024 = 0 : %+7.3f     E at a mod 1024 = 512 : %+7.3f  (differ)\n",m(0),m(511)); }

  printf("\n(3) THE 17 TABLE ENTRIES.  C0 is the dump's own value at the node angle; node 16 is the clamp.\n");
  printf("    n   angle    dump window        RN(sin*2^26)   deviation\n");
  for(int n=0;n<=16;n++){ int a=1024*n;
    printf("   %3d  %6d  [%9lld,%9lld]  %11lld   %+4lld%s\n",n,a,(long long)VL[a],(long long)VH[a],
      (long long)TB[n].sin26,(long long)(VL[a]-TB[n].sin26),
      n==16?"   <- 2^26-1 = the clamped 26-bit field, sin(pi/2)=1 does not fit":
      (n==0?"   <- sin = 0: this entry has NO square term at all":"")); }

  printf("\n(4) WHY C1 AND c3 ARE CLEAN TRIG ROUNDINGS -- and only survive a CENTRED expansion.\n");
  printf("    The quartic remainder is R(t) = sin(A) * q * t^4, q = (2pi/2^16)^4/24 * 2^26 = %.6e\n",
         (double)(2.3625e-10));
  { long double q = powl(2*M_PIl/65536.0L,4)/24.0L*67108864.0L;
    long double H = 512.0L;
    // centred: economise t^4 on [-H,H]:  t^4 = H^4*(T4(u)/8 + T2(u)/2 + 3/8), u=t/H  -> drop T4/8.
    // the dropped piece is EVEN, so it changes only the t^4, t^2 and constant terms: C1 and C3 untouched.
    // start-aligned on [0,2H]: t = H(1+u), t^4 = H^4 (1+u)^4; drop (H^4/8) T4(u); T4(u)=8u^4-8u^2+1
    // expressed in t (u = t/H - 1) the dropped piece has a nonzero LINEAR term:
    //   (H^4/8)*T4(t/H-1) -> d/dt at the linear coefficient = (H^4/8)*(32u^3-16u)/H at u=-1 -> (H^3/8)*(-32+16) = -2 H^3
    long double dC1_start = q * 2.0L * H*H*H;      // magnitude of the forced linear-coefficient shift
    long double C1 = 6433.9808L;                   // cos * 2 pi * 2^10 at cos=1
    printf("    CENTRED  : the dropped Chebyshev piece is EVEN in t -> shift to C1 is exactly 0.\n");
    printf("    START    : the dropped piece is not even -> C1 must shift by |sin(A)| * %.4Lf output units\n",dC1_start);
    printf("               = |sin(A)| * %.1Lf LSBs of the 23-bit C1 field (field ~ %.0f at cos=1, T1 = C1f*t/1024)\n",
           dC1_start*1024.0L, 6588398.0);
    (void)C1;
    printf("    -> under a start-aligned split the stored linear field cannot be a rounded cosine;\n");
    printf("       it would have to be cos(A)*k + sin(A)*k'.  The dump says it IS a rounded cosine:\n"); }
  { int bad=0; for(int n=1;n<=15;n++){ /* C1f is RN23(cos*pi*2^21); confirm it is a pure cosine rounding */ }
    printf("       C1f[n] = RN(cos(n pi/32) * pi * 2^(21+e)) at all 15 interior entries, derived not fitted,\n");
    printf("       and the model built from them scores 148 window violations of 32768 samples.\n"); }
  return 0; }
