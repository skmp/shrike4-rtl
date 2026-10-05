// Isolate the square using equal offsets at mirror nodes n and 32-n.
// Conditional on common C0/even path and sign-reversed odd paths.
#include "fsca_common.h"
#include "../fsca_table.h"
int main(){Fsca f;if(!load_fsca(f))return 2;
 for(int n=1;n<=15;++n){int exact=0,odd=0,empty=0,asym=0;std::vector<i64> l(1024),h(1024);
 for(int t=-512;t<=511;++t){Win a=decode(f.srz[1024*n+t],f.srn[1024*n+t]),b=decode(f.srz[1024*(32-n)+t],f.srn[1024*(32-n)+t]);
 i64 lo=a.lo+b.lo,hi=a.hi+b.hi;odd+=lo==hi&&(lo&1);
 // Integer squared term S = C0 - (Vn+Vmirror)/2.
 l[t+512]=FSCA_TAB[n][0]-hi/2;h[t+512]=FSCA_TAB[n][0]-(lo+1)/2;
 exact+=l[t+512]==h[t+512];empty+=l[t+512]>h[t+512];}
 for(int t=1;t<=511;++t)asym+=l[512+t]>h[512-t]||l[512-t]>h[512+t];
 printf("node %d square singleton %d empty %d exact odd sums %d disjoint +/- pairs %d\n",n,exact,empty,odd,asym);
 }
}
