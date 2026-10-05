// rr14_exact.cpp -- the FSRRA structural model after rr10 (fixed-point square field) and rr13 (the cubic chain reads the
// SAME square latch as the square term), scored on every input of [1,4) in both rounding modes' pinned value:
//   dx   = t << 4                                   t = 19-bit signed offset from the rounded node
//   sq   = (arr(dx,dx) + K) >> 23                   ONE square latch (product register >>> 3)
//   T1   = sm(arr(REC1, dx) + K, 26 - e1)           REC1 23-bit normalised, 2-bit exponent e1
//   T2   = sm(arr(C2f << 9, sq) + K, 29)            C2f 14-bit FIXED-POINT field, no exponent
//   I    = (arr(C3f << 8, sq) + K) >> 23            C3f  9-bit FIXED-POINT field
//   T3   = sm(arr(dx, I) + K, 27)
//   W    = C0 - T1 + T2 - T3
// Words from refit2.txt (C0, REC1/e1, C2f = rec2>>9, C3f = rec3>>10).  Writes words_final.txt.
#include "rr_common.h"
#include <omp.h>
int main(){ if(!load_fsrra())return 1; Word REF[34]; if(!load_words("refit2.txt",REF))return 1; long miss[34]={0},ns[34]={0};
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ Word w=REF[id]; i64 c2=(w.rec2>>9)<<9, c3=(w.rec3>>10)<<8; auto S=node_samples(id/17,id%17,1);
        for(auto&s:S){ i64 I=(arr(c3,s.sq21)+K)>>23; i64 W=w.C0-sm(arr(w.rec1,s.dx)+K,26-w.e1)+sm(arr(c2,s.sq21)+K,29)-sm(arr(s.dx,I)+K,27); ns[id]++; if(!okd(s,W-s.q4))miss[id]++; } }
    long tm=0,tn=0; FILE*o=fopen("words_final.txt","w"); fputs("# par n C0 REC1 e1 C2f C3f\n",o);
    for(int id=0;id<34;id++){ tm+=miss[id]; tn+=ns[id]; Word&w=REF[id]; printf(" %d %2d : %ld / %ld    C0 %9lld  REC1 0x%06llx e1 %d  C2f %5lld (0x%04llx)  C3f %3lld (0x%03llx)\n",id/17,id%17,miss[id],ns[id],(long long)w.C0,(long long)w.rec1,w.e1,(long long)(w.rec2>>9),(long long)(w.rec2>>9),(long long)(w.rec3>>10),(long long)(w.rec3>>10)); fprintf(o,"%d %d %lld %lld %d %lld %lld\n",id/17,id%17,(long long)w.C0,(long long)w.rec1,w.e1,(long long)(w.rec2>>9),(long long)(w.rec3>>10)); }
    fclose(o); printf("TOTAL %ld misses of %ld pinned samples\n",tm,tn); return 0; }
