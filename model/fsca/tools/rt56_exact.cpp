// Exact-model acceptance and global-constant identifiability. Integer only.
// Constant sweeps are DIAGNOSTIC; the model uses a single recovered ROM value.
#include "base2.h"
#include "../sh4_fsca.h"   // the delivered reference (was ../fsca_ref2.h, now archived)
#define FSCA2_KC ((i64)SH4_FSCA_KC)
#define FSCA2_C0(m) ((i64)sh4_fsca_rom[m].c0)
#include <algorithm>
static i64 candidate(unsigned angle,i64 kc,int roles){
 int q=(angle+512)>>10,m=q&63,t=int(angle)-1024*q,neg=0,flip=0;
 if(m>=33){m-=32;neg=1;}if(m>=17){m=32-m;flip=1;}
 i64 dx=i64(t)*8192;
 i64 odd=sm(arr(TB[m].C1f,dx)+K,23-TB[m].e1)-sm(arr(TB[m].c3*32,(arr(i64(t)*t*8,dx)+K)>>23)+K,26);
 i64 i=((roles&1?arr(dx,kc):arr(kc,dx))+K)>>23;
 i64 j=((roles&2?arr(i,dx):arr(dx,i))+K)>>23;
 i64 c=TB[16-m].C1f;
 i64 even=c?sm((roles&4?arr(j,c):arr(c,j))+K,27-TB[16-m].e1):0;
 i64 v=FSCA2_C0(m)+(flip?-odd:odd)-even;return neg?-v:v;
}
int main(){Fsca f;if(!load_fsca(f))return 1;build_tables();int fail=0,win=0,guards[5]={},rn=0,rz=0,crn=0,crz=0,ref=0;
for(unsigned a=0;a<65536;a++){i64 v=candidate(a,FSCA2_KC,0);if(v!=sh4_fsca_internal(a))ref++; auto w=decode(f.srz[a],f.srn[a]);bool bad=w.sign>0?(v<w.lo||v>w.hi):(v< -w.hi||v> -w.lo);win+=bad;
 int n=((a+512)>>10)&63;int g[]={0,1,2,16,32};for(int k=0;k<5;k++)if(n==g[k])guards[k]+=bad;
 rn+=to_float(v,0)!=f.srn[a];rz+=to_float(v,1)!=f.srz[a];crn+=to_float(candidate((a+16384)&65535,FSCA2_KC,0),0)!=f.crn[a];crz+=to_float(candidate((a+16384)&65535,FSCA2_KC,0),1)!=f.crz[a];}
printf("full 65536: sin RN %d RZ %d cos RN %d RZ %d windows %d independent-vs-header %d\n",rn,rz,crn,crz,win,ref);printf("guards nodes 0/1/2/16/32: %d %d %d %d %d\n",guards[0],guards[1],guards[2],guards[3],guards[4]);fail=rn+rz+crn+crz+win+ref;
for(int role=0;role<8;role++){int v=0;for(unsigned a=0;a<32768;a++){i64 z=candidate(a,FSCA2_KC,role);auto w=decode(f.srz[a],f.srn[a]);v+=z<w.lo||z>w.hi;}printf("roles xor %d window violations first half-turn %d\n",role,v);}
int exact=0;for(i64 kc=FSCA2_KC-128;kc<=FSCA2_KC+128;kc++){int bad=0;for(unsigned a=0;a<32768;a++){auto w=decode(f.srz[a],f.srn[a]);i64 z=candidate(a,kc,0);bad+=z<w.lo||z>w.hi;if(bad&&std::abs(kc-FSCA2_KC)>1)break;}if(!bad){printf("admissible KC %lld = 0x%llx\n",(long long)kc,(long long)kc);exact++;}if(std::abs(kc-FSCA2_KC)<=1)printf("KC delta %+lld: %d violations\n",(long long)(kc-FSCA2_KC),bad);}
printf("admissible constants in +/-128: %d\n",exact);
// Rounding the old effective quadratic constant after factoring out pi/2
// independently recovers the compact 17-bit word, but is NOT proof of the
// silicon's original approximation/generation algorithm.
i64 compact=(i64)(((i128)KC*4*QONE/PI_()+32)/64);
printf("RN((old KC * 4/pi)/64) = %lld; recovered KC = %lld << 6\n",(long long)compact,(long long)(FSCA2_KC>>6));
return fail?1:0;}
