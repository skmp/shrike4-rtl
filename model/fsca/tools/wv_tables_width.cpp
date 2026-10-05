// wv_tables_width.cpp -- the operand-width objection, measured.
// The FIPR multiplier is 24x24 -> 28 (fipr/README.md); fsca_common.h's validated mult28 has 13 Booth
// rows (k = 0..12) and its operands are documented "< 2^24".  A 13-row Booth recoding is only correct
// for an unsigned operand < 2^25 (else the top digit goes negative with no row above it to correct it).
#include "wv_tables_core.h"
#include <cstdio>
// the validated 13-row array (rows 0..12), same rules; equals wv_arr only while the operand fits
static i64 arr13(i64 rec, i64 mul) {
    i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) {
        auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d = -2*bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d<0?-d:d)*mul; i64 row = d<0 ? ~mag : mag;
        R[k] = row * (1ll<<(2*k)); T += (R[k]>>16)<<16; if (d<0 && 2*k>=18) T += 1ll<<(2*k);
    }
    int m15 = ((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T += (1ll<<16)*(m15>=2);
    int p16 = ((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T -= (1ll<<16)*p16;
    return T;
}
int main(){ if(!load_fsca(WF)) return 1; wv_build();
  puts("s2  t's with latch >= 2^24 (over the 24-bit operand latch)  >= 2^25 (mis-recoded by a 13-row array)  M1 with the validated 13-row array   M1 with the 14-row extension");
  for (int s2 : {21,20,19,18,17}) {
    WCfg C; C.s2=s2; C.k2=0; int o24=0,o25=0,m13=0,m14=0;
    for (int t=-512;t<=511;t++){ i64 J=wv_I2(C,t); if(J>=(1ll<<24))o24++; if(J>=(1ll<<25))o25++; }
    for (int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
      for (int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 lo,hi; bool ex; wv_win(a,lo,hi,ex);
        i64 J=wv_I2(C,t); i64 base=wv_base(n,t,flip); int u=wv_u2(C,n);
        i64 V14 = base - (WT[n].S? wv_sm(wv_arr(J,WT[n].S)+WK,u):0);
        i64 V13 = base - (WT[n].S? wv_sm(arr13(J,WT[n].S)+WK,u):0);
        if(V14<lo||V14>hi)m14++; if(V13<lo||V13>hi)m13++; } }
    printf("%2d            %4d                                    %4d                              %6d                        %4d\n",s2,o24,o25,m13,m14);
  }
  // and the same for the FIRST chain multiply's operand (KC, 23 bits) and the second (I1)
  { WCfg C; i64 mx=0; for(int t=-512;t<=511;t++){ i64 v=wv_I1(C,t); i64 m=v<0?-v:v; if(m>mx)mx=m; }
    printf("\nfor reference: |I1| max %lld (%d bits), KC = %lld (23 bits), S max %lld (23 bits)\n",
      (long long)mx, (int)(64-__builtin_clzll(mx)), (long long)WKC, (long long)WT[16].S); }
  return 0; }
