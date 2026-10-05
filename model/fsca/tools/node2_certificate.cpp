// A feasibility witness, NOT a proposed hardware implementation.
// Refutes the older claim that even per-side product constants cannot fit node 2.
#include "fsca_common.h"
#include "../archive/fsca_ref.h"
int main(){Fsca f;if(!load_fsca(f))return 2;int badrn=0,badrz=0;
 for(int n:{2,30,34,62})for(int t=-512;t<=511;++t){
 i64 linear=(i64(0xc532d6)*t+64)/(1ll<<11);
 i64 square=(i64(32298060)*t*t+(t<0?0:-1715949))/(1ll<<29);
 i64 cubic=(i64(10629120)*t*t*t+(t<0?4947802324ll:6996099072ll))/(1ll<<40);
 i64 v=(n==2||n==34)?13092290+linear-square-cubic:13092290-linear-square+cubic;
 if(n>=32)v=-v;int i=n*1024+t;
 badrn+=fsca_to_float(v,0)!=f.srn[i];badrz+=fsca_to_float(v,1)!=f.srz[i];
 }
 printf("node-2 family, 4096 angles: RN %d, RZ %d mismatches\n",badrn,badrz);return badrn||badrz;
}
