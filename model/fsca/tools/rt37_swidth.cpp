// rt37_swidth.cpp -- the WIDTH of the quadratic (sine) field.  rt36: the data barely resolves S's low bits (+-1 LSB:
// 148 -> 163), unlike C1F / c3 / C0 which are pinned to the last bit.  S_W = RN_W or floor_W of sin(n pi/32), normalised
// to W bits, aligner shift 7 + W + k.  Also the same with the square latch J carried with e extra bits (a narrower
// S leaves room for a wider J on a 24-bit port).  Full model; guard nodes 1,2,16,30,31.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 LO[32][1024],HI[32][1024],BASE[32][1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=1;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1; for(int i=0;i<1024;i++){ int t=i-512; i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0; BASE[m][i]=C0TAB[n]+s*T1-s*T3;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){LO[m][i]=w.lo;HI[m][i]=w.hi;} else {LO[m][i]=-w.hi;HI[m][i]=-w.lo;} } }
    puts("  W  rounding   J extra bits e |  violations  guard");
    for(int W=16;W<=26;W++) for(int rmode=0;rmode<2;rmode++) for(int e=0;e<=2;e++){
        i64 Sw[17]; int kk[17]; for(int n=1;n<=16;n++){ i128 a=PI_()*n/32; i128 s=sin_(a); int k=0; while(s*((i128)1<<(k+1))<QONE)k++; kk[n]=k; i128 num=s<<(W+k); i64 q=(i64)(num/QONE); i128 r=num%QONE; if(rmode==0&&(r*2>QONE||(r*2==QONE&&(q&1))))q++; if(q>=(1ll<<W))q=(1ll<<W)-1; Sw[n]=q; }
        static i64 J[1024]; bool bad=false; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J[t+512]=(arr(I1,dx)+K)>>(20-e); if(J[t+512]>=(1ll<<27))bad=true; } if(bad)continue;
        int v=0,g=0; for(int m=1;m<32;m++){ int n=m>=17?32-m:m; bool gd=(m<=2||m==16||m>=30); int u=7+W+kk[n]+e; for(int i=0;i<1024;i++){ i64 T2=sm(arr(J[i],Sw[n])+K,u); i64 V=BASE[m][i]-T2; if(V<LO[m][i]||V>HI[m][i]){v++; if(gd)g++;} } }
        printf(" %2d  %-8s        %d        |   %6d    %4d%s\n",W,rmode?"floor":"nearest",e,v,g,(W==23&&rmode==0&&e==0)?"   <- the model":""); }
    return 0; }
