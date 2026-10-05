// fv_final-fields_7_ff.cpp -- INDEPENDENT check of the two robust sub-claims of the fraction-feedback item:
//   (a) at t = 0 BOTH odd terms vanish for all 17 nodes, so the t=0 dump windows test the merge with no freedom
//   (b) the operand-width / resolution argument: the fine latch J(17)=45617 needs ~35 fraction bits, and at
//       m = 24 fraction bits the square term resolves only ~22 quanta at t = 17.
// All integer; long double only to print a scale.
#include "base2.h"
#include <cstring>
int main(){ build_tables();
    puts("== (a) do both odd terms vanish at t = 0? ==");
    { int nz=0; for(int n=0;n<=16;n++){ i64 dx=0;
        i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
        i64 A3=((arr(0,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
        if(T1||T3){ nz++; printf("   node %d: T1=%lld T3=%lld\n",n,(long long)T1,(long long)T3); } }
      printf("   nodes with a nonzero odd term at t=0: %d of 17\n",nz); }
    puts("\n== (b) resolution of the square's fraction field ==");
    // The square term in turns:  delta = t/2^16 ;  delta^2/2 = 2*pi^2*t^2 / 2^32 (exact rational, integer-checked)
    // The fine latch:  J = (delta^2/2) * 2^41 / ... -- measured directly against the required value.
    { // integer Q60 computation of 2*pi^2*t^2*2^(sh-32)
      auto val=[&](int t,int sh)->long double{ i128 pi=PI_(); i128 p2=pi*pi/QONE;       // pi^2 in Q60
          i128 v=2*p2; v=v*t/1*t;                                                        // 2*pi^2*t^2 in Q60
          // multiply by 2^(sh-32), then out of Q60
          long double r=(long double)v/(long double)QONE; return r*powl(2.0L,sh-32); };
      printf("   2*pi^2*(t/2^16)^2 * 2^34 at t=17 = %.1Lf\n",val(17,34));
      printf("   2*pi^2*(t/2^16)^2 * 2^35 at t=17 = %.1Lf   (required fine latch J(17) = 45617)\n",val(17,35));
      // quanta available at m fraction bits: floor(delta^2/2 * 2^m) computed as an exact integer
      for(int m=20;m<=38;m+=2){ i128 pi=PI_(); i128 p2=pi*pi/QONE; i128 v=2*p2*17*17;    // Q60, = 2 pi^2 t^2
          // delta^2/2 = 2*pi^2*t^2/2^32 ; times 2^m -> shift
          i128 num = (m>=32)? (v<<(m-32)) : (v>>(32-m)); i64 q=(i64)(num/QONE);
          printf("   m = %2d fraction bits -> the square resolves %lld quanta at t = 17%s\n",m,(long long)q,
                 m==24?"   <-- FIPR's operand width":(q>=45617?"   <-- enough to resolve 45617":"")); } }
    puts("\n== (c) bookkeeping: does 592704 factor as the described product? ==");
    { long p = 7L*4*2*19*3*9*2*7*2;
      printf("   W(7) x rnd(4) x norm/fixed(2) x m 18..36 (19) x b(3) x a +-4 (9) x role(2) x K3(7) x cplx(2) = %ld\n",p);
      printf("   reported 592704 = 2^6 * 3^3 * 7^3 -- has no factor 19, so the stated product is not the count\n"); }
    return 0; }
