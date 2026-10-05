// rt42_quantised.cpp -- POLYNOMIAL-WITH-COEFFICIENT-ROM or ROTATION / ANGLE-ADDITION?
// A rotation scheme (three shears, or "small polynomial in d, then a rotation step") computes the offset functions
// once, node-independently; the node enters only through the table values s, c.  Its effective coefficients are DERIVED
// products (c*k, c*k^3/6): never rounded per node on their own grid.  A coefficient ROM holds each coefficient rounded
// independently.  Scan each odd coefficient at SUB-LSB resolution on the exact-valued nodes and see which the data wants.
//   cubic : rec = (c3 << 5) + 2 j      j in -16..16  <->  c3 + j/16      (unit column unchanged)
//   linear: rec = C1F*8 + j, unit +3   j in -8..8    <->  C1F + j/8
// Markers: R = the independently rounded value (j = 0); T = the unrounded Taylor value; E = the unrounded economised one.
#include "base2.h"
#include <cmath>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); long double PI=acosl(-1.0L), h=512*2*PI/65536;
    static i64 Jc[1024],A3c[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; A3c[t+512]=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; }
    for(int which=0;which<2;which++){ printf("\n%s coefficient, sub-LSB scan: violations on the node and its mirror (2048 samples)\n",which?"LINEAR (C1F, 23 bit)":"CUBIC (c3, 12 bit)");
        int R=which?8:16;
        for(int n=0;n<=4;n++){ const Tab&T=TB[n]; long double c=cosl(PI*n/32);
            long double trueval = which? c*PI/2*powl(2,22)*powl(2,-T.e1) : c*powl(PI,3)*512/6; long double econ = which? trueval*(1-powl(h,4)/384) : trueval*(1-h*h/16);
            double jt=(double)((trueval-(which?T.C1f:T.c3))*R), je=(double)((econ-(which?T.C1f:T.c3))*R);
            printf("  node %d (%s): unrounded Taylor at j=%+.1f, economised at j=%+.1f\n     j:",n,n<=2?"EXACT values":"windows 1-2 wide",jt,je);
            int best=1<<30,bj=0; std::vector<int> vv;
            for(int j=-R;j<=R;j++){ int v=0; for(int i=0;i<(n==0?1:2);i++){ int m=i?32-n:n; if(n==0&&i==0)m=0; int flip=m>=17; i64 s=flip?-1:1;
                    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1,T3;
                        if(which){ T1=sm(arr(T.C1f*8+j,dx)+K,23-T.e1+3); T3=T.c3? sm(arr(T.c3<<5,A3c[t+512])+K,26):0; } else { T1=sm(arr(T.C1f,dx)+K,23-T.e1); T3=sm(arr((T.c3<<5)+2*j,A3c[t+512])+K,26); }
                        i64 T2=T.S? sm(arr(Jc[t+512],T.S)+K,T.u2):0; i64 V=C0TAB[n]+s*T1-T2-s*T3; int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} if(V<lo||V>hi)v++; } }
                vv.push_back(v); if(v<best){best=v;bj=j;} }
            for(int j=-R;j<=R;j++){ int v=vv[j+R]; char mk=' '; if(j==0)mk='R'; if(j==(int)lround(jt))mk=(mk=='R')?'*':'T'; if(j==(int)lround(je)&&mk==' ')mk='E'; if(j%(which?1:2)==0||mk!=' ') printf(" %d%c",v,mk); }
            printf("\n     minimum %d at j=%+d\n",best,bj); } }
    return 0; }
