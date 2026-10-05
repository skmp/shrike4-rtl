// rr15_unique.cpp -- with the rr14 model exact, how well is every ROM field pinned?  Per node: the misses when ONE field
// moves by -2..+2 LSB (others fixed); then the one remaining miss of rr14 in detail; then (1,16)'s REC1, which comes out
// BELOW 2^22 at e1 = 0 (0x3ffff1): try it as a normalised field one exponent lower (REC1' = 2*REC1 + j, shift + 1).
#include "rr_common.h"
#include <omp.h>
static inline i64 W5(i64 C0,i64 r1,int u1,i64 c2,i64 c3,const Smp&s){ i64 I=(arr(c3<<8,s.sq21)+K)>>23; return C0-sm(arr(r1,s.dx)+K,u1)+sm(arr(c2<<9,s.sq21)+K,29)-sm(arr(s.dx,I)+K,27); }
int main(){ if(!load_fsrra())return 1; Word REF[34]; if(!load_words("refit2.txt",REF))return 1; static long M[34][4][5];
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ Word w=REF[id]; auto S=node_samples(id/17,id%17,1); for(int f=0;f<4;f++)for(int d=-2;d<=2;d++){ long m=0; for(auto&s:S){ i64 W=W5(w.C0+(f==0?d:0),w.rec1+(f==1?d:0),26-w.e1,(w.rec2>>9)+(f==2?d:0),(w.rec3>>10)+(f==3?d:0),s); if(!okd(s,W-s.q4))m++; } M[id][f][d+2]=m; } }
    puts("misses when one field moves by d = -2 -1 0 +1 +2 LSB:\npar  n |            C0                 |            REC1               |            C2f                |            C3f");
    for(int id=0;id<34;id++){ printf(" %d  %2d |",id/17,id%17); for(int f=0;f<4;f++){ for(int d=0;d<5;d++)printf(" %5ld",M[id][f][d]); printf(" |"); } puts(""); }
    { Word w=REF[0]; auto S=node_samples(0,0,1); for(auto&s:S){ i64 W=W5(w.C0,w.rec1,26-w.e1,w.rec2>>9,w.rec3>>10,s); if(!okd(s,W-s.q4)) printf("the miss: (0,0) t = %lld  model W = %lld  RZ mantissa*4 = %lld  allowed low bits mask 0x%x  RZ word %08x RN word %08x\n",(long long)s.t,(long long)W,(long long)s.q4,s.cm,RZ[s.t],RN[s.t]); } }
    { Word w=REF[33]; auto S=node_samples(1,16,1); puts("(1,16) REC1 as a normalised field one exponent lower: REC1' = 2*0x3ffff1 + j, aligner shift 27"); for(int j=-3;j<=3;j++){ long m=0; for(auto&s:S){ i64 W=W5(w.C0,2*w.rec1+j,27,w.rec2>>9,w.rec3>>10,s); if(!okd(s,W-s.q4))m++; } printf("   j = %+d (0x%06llx): %ld misses\n",j,(long long)(2*w.rec1+j),m); } }
    return 0; }
