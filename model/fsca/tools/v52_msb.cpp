// v52_msb.cpp -- B(ii): can the node be WIDER than 1024 angles?  Model-free 5-point cubic certificate.
// For any cubic p and any h:  p(x-2h) - 4p(x-h) + 6p(x) - 4p(x+h) + p(x+2h) = 0 identically.
// The dump pins each internal value to an interval; if the achievable interval of that combination
// excludes 0, NO cubic fits those five samples, and the best one misses by at least |D|min/16.
#include "v52_common.h"
static Fsca F;
static void win_at(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a&0xffff],F.srn[a&0xffff]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;} }
int main(){ if(!load_fsca(F))return 1; build_tables();
  // sanity: how wide are the windows?
  { i64 mx=0; long long sum=0; for(int a=0;a<65536;a++){i64 l,h;win_at(a,l,h);mx=std::max(mx,h-l);sum+=h-l;}
    printf("dump window widths (hi-lo): max %lld, mean %.4f  -> slack of the 5-point combo <= 16*max = %lld\n",
      (long long)mx,(double)sum/65536.0,(long long)(16*mx)); }
  int cs[]={1,-4,6,-4,1};
  for(int node_ang : {1024,2048,4096}){
    int h = node_ang/4;                 // stencil spans +-2h = +- node_ang/2 = the whole node
    for(int phase=0;phase<2;phase++){
      int ncent = 65536/node_ang; int excl=0; i64 worst=0; int worst_c=0;
      for(int k=0;k<ncent;k++){
        int c = k*node_ang + (phase? node_ang/2 : 0);
        i64 dlo=0,dhi=0;
        for(int i=0;i<5;i++){ int a=c+(i-2)*h; i64 l,hh; win_at(a,l,hh);
          if(cs[i]>0){ dlo+=cs[i]*l; dhi+=cs[i]*hh; } else { dlo+=cs[i]*hh; dhi+=cs[i]*l; } }
        if(dlo>0||dhi<0){ excl++; i64 m = dlo>0? dlo : -dhi; if(m>worst){worst=m;worst_c=c;} } }
      printf("node width %4d angles (h=%4d, phase=%d): %2d/%2d centres EXCLUDE a cubic; worst |D|=%lld at centre %d -> bound %.2f units\n",
        node_ang,h,phase,excl,ncent,(long long)worst,worst_c,(double)worst/16.0); } }
  puts("");
  puts("Chebyshev economisation of the missing quartic (scale check, long double, labelled as an estimate):");
  for(int hw : {256,512,1024}) printf("   half-width %4d angles: (2*pi*hw/65536)^4/192 * 2^26 = %.4Lf units\n",
      hw, powl(2*acosl(-1.0L)*hw/65536.0L,4)/192.0L*67108864.0L);
  return 0; }
