// w3m_w14coarse.cpp -- the w=14 "one extra bit in the operand register" variant at the COARSE published
// scale, both operand orders, with guard-node counts measured in properly sequenced calls.
#include "w3_common.h"
int main(){ if(!load_fsca(FD))return 1; build_tables(); DU=0;
    struct C{const char*n;int w,s1,s2,ord;} cs[]={
      {"w=13 s1=23 s2=20 arr(dx,KC)  [147]",13,23,20,1},{"w=13 s1=23 s2=20 arr(KC,dx)  [148]",13,23,20,0},
      {"w=14 s1=24 s2=21 arr(dx,KC)",14,24,21,1},{"w=14 s1=24 s2=21 arr(KC,dx)",14,24,21,0},
      {"w=15 s1=25 s2=22 arr(KC,dx)",15,25,22,0},{"w=16 s1=26 s2=23 arr(KC,dx)",16,26,23,0},
      {"w=12 s1=22 s2=19 arr(KC,dx)",12,22,19,0}};
    for(auto&c:cs){ int W=c.w,S1=c.s1,S2=c.s2,O=c.ord;
        auto f=[W,S1,S2,O](int t)->i64{ i64 dx=(i64)t<<W; i64 I1=((O?arr(dx,KC):arr(KC,dx))+K)>>S1; return (arr(I1,dx)+K)>>S2; };
        int g; int v=full_violations(f,&g);
        printf("%-36s : %5d violations, guard(0,1,2,16) %d\n", c.n, v, g); }
    return 0; }
