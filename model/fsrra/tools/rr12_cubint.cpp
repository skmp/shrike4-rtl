// rr12_cubint.cpp -- after rr10/rr11 (fixed-point square field) ALL 513 remaining misses have the cubic product within
// 2^22 raw (= one step of the chain intermediate I times dx) of its truncation boundary.  So: is it I that is off by one?
// For each miss: the direction I would have to move, and where the intermediate (arr(rec3, sq19) + K) sits inside its
// 2^23 truncation step (in 1/64ths).  Would I +- 1 repair the sample?
#include "rr_common.h"
int main(){ if(!load_fsrra())return 1; Word REF[34]; if(!load_words("refit2.txt",REF))return 1; long hist[2][64]={{0}}; long fixed=0,notfixed=0,tot=0; long byC3[400][2]={{0}};
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word w=REF[id]; auto S=node_samples(par,n,1);
        for(auto&s:S){ i64 X=arr(w.rec3,s.sq19)+K; i64 I=X>>23; i64 T1=termT1(w,s),T2=termT2(w,s); auto Wof=[&](i64 i){ return w.C0-T1+T2-sm(arr(s.dx,i)+K,27); }; if(okd(s,Wof(I)-s.q4))continue; tot++;
            int fr=(int)((X&((1ll<<23)-1))>>17); bool up=okd(s,Wof(I+1)-s.q4), dn=okd(s,Wof(I-1)-s.q4); if(up)hist[0][fr]++; if(dn)hist[1][fr]++; if(up||dn)fixed++; else notfixed++; byC3[w.rec3>>10][up?0:1]++; } }
    printf("%ld misses: %ld repaired by I+-1, %ld not\n",tot,fixed,notfixed);
    puts("position of the intermediate inside its truncation step (1/64ths):  [needs I+1]  [needs I-1]"); for(int b=0;b<64;b++) if(hist[0][b]||hist[1][b]) printf("   %2d/64 : %5ld %5ld\n",b,hist[0][b],hist[1][b]);
    puts("by C3f (needs I+1 / I-1):"); for(int c=0;c<400;c++) if(byC3[c][0]||byC3[c][1]) printf("  C3f %3d = 0x%03x : %3ld / %3ld\n",c,c,byC3[c][0],byC3[c][1]);
    return 0; }
