// rr16_endnodes.cpp -- the two one-sided end words whose REC1 is not unique (rr15): the admissible REC1 range of (0,0)
// [t >= 0 only] and (1,16) [t < 0 only], everything else fixed at the final words.
#include "rr_common.h"
static inline i64 W5(i64 C0,i64 r1,int u1,i64 c2,i64 c3,const Smp&s){ i64 I=(arr(c3<<8,s.sq21)+K)>>23; return C0-sm(arr(r1,s.dx)+K,u1)+sm(arr(c2<<9,s.sq21)+K,29)-sm(arr(s.dx,I)+K,27); }
int main(){ if(!load_fsrra())return 1; Word REF[34]; if(!load_words("refit2.txt",REF))return 1;
    for(int id: {0,33}){ Word w=REF[id]; auto S=node_samples(id/17,id%17,1); printf("(%d,%d) e1 = %d: REC1 values with no real miss:",id/17,id%17,w.e1); i64 first=-1,last=-1;
        for(i64 r=w.rec1-40;r<=w.rec1+80;r++){ long m=0; for(auto&s:S){ if(s.t==0&&id==0)continue; if(!okd(s,W5(w.C0,r,26-w.e1,w.rec2>>9,w.rec3>>10,s)-s.q4)){m++;break;} } if(!m){ if(first<0)first=r; last=r; } }
        printf(" 0x%06llx .. 0x%06llx%s\n",(long long)first,(long long)last,last==w.rec1+80?" (scan limit)":""); }
    return 0; }
