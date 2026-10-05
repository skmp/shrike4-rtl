// w3c_form.cpp -- BRANCH 3 (b),(c),(d): how the offset reaches the square path's array.
//   (b) zero-extension / wrong-width sign-extension of the 10-bit signed t
//   (c) the square path reads FEWER significant bits of t (low-bit truncation, 4 rounding modes)
//   (d) the square path does not subtract the node base, or subtracts a different one
// Placement: the mangled operand is used in multiply 1 only, multiply 2 only, or both.
#include "w3_common.h"
#include <cstdlib>
static const char* PL[3] = {"mul1", "mul2", "both"};
struct Res { int score; i64 j17,j342,jm342; int hits; };
template<class F> static Res eval(F mk, int pl){
    auto Jf=[&](int t){ i64 d0=(i64)t<<13, dm=mk(t)<<13;
        i64 a=(pl==1? d0:dm), b=(pl==0? d0:dm);
        i64 I1=(arrN(KC,a,ND_SQ1)+K)>>23; return (arrN(I1,b,ND_SQ2)+K)>>18; };
    Res r; r.score=score_jwin(Jf,JLf,JHf); r.j17=Jf(17); r.j342=Jf(342); r.jm342=Jf(-342);
    r.hits = (r.j17==45617) + (r.j342==18466836) + (r.jm342>=18466858 && r.jm342<=18466867);
    return r; }
static void line(const char* tag,int pl,Res r,int base=918){
    printf("  %-34s %-4s  %4d %+4d   %-9lld %-9lld %-9lld  pins=%d%s\n", tag, PL[pl], r.score, r.score-base,
        (long long)r.j17,(long long)r.j342,(long long)r.jm342, r.hits, r.hits? "  <== LANDMARK":""); }
int main(){ if(!load_fsca(FD))return 1; build_tables(); if(!load_jwin())return 2;
    printf("baseline 918/1024, J(17)=45630 (need 45617), J(342)=18466846 (need 18466836), J(-342)=18466857 (need 18466858..67)\n\n");
    printf("(b) EXTENSION OF THE 10-BIT SIGNED t INTO THE OPERAND REGISTER\n");
    printf("  %-34s %-4s  %4s %4s   %-9s %-9s %-9s\n","variant","plc","scor","d","J(17)","J(342)","J(-342)");
    for(int W=9;W<=20;W++){ char b[64]; snprintf(b,64,"zero-extend at width %d",W);
        for(int pl=0;pl<3;pl++) line(b,pl,eval([&](int t)->i64{ return t<0? (i64)t+(1ll<<W) : t; },pl)); }
    for(int W=6;W<=11;W++){ char b[64]; snprintf(b,64,"sign-extend from bit %d",W-1);
        for(int pl=0;pl<3;pl++) line(b,pl,eval([&](int t)->i64{ i64 m=(1ll<<W)-1; i64 x=t&m; return (x^(1ll<<(W-1)))-(1ll<<(W-1)); },pl)); }
    printf("\n(c) SQUARE PATH READS FEWER SIGNIFICANT BITS OF t  (drop b low bits of a 10-bit signed t)\n");
    printf("  %-34s %-4s  %4s %4s   %-9s %-9s %-9s\n","variant","plc","scor","d","J(17)","J(342)","J(-342)");
    for(int b=1;b<=7;b++){
        char n1[64],n2[64],n3[64],n4[64]; int k=11-b;
        snprintf(n1,64,"k=%d bits, floor (>>b<<b)",k); snprintf(n2,64,"k=%d bits, round-to-nearest",k);
        snprintf(n3,64,"k=%d bits, toward zero",k);    snprintf(n4,64,"k=%d bits, round-half-away",k);
        for(int pl=0;pl<3;pl++){
          line(n1,pl,eval([&](int t)->i64{ return ((i64)t>>b)<<b; },pl));
          line(n2,pl,eval([&](int t)->i64{ return (((i64)t+(1ll<<(b-1)))>>b)<<b; },pl));
          line(n3,pl,eval([&](int t)->i64{ i64 a=llabs((long long)t); a=(a>>b)<<b; return t<0?-a:a; },pl));
          line(n4,pl,eval([&](int t)->i64{ i64 a=llabs((long long)t); a=((a+(1ll<<(b-1)))>>b)<<b; return t<0?-a:a; },pl));
        } }
    printf("\n(d) SQUARE PATH USES A DIFFERENT BASE  (node-dependent J -> full-model score only)\n");
    printf("  baseline full model at this (fine) scale: ");
    { auto Jb=[&](int nd,int t){ (void)nd; return baseline_J(t); }; int g; printf("%d violations, guard %d\n", full_violations_nt(Jb,&g), g); }
    for(int mode=0;mode<3;mode++){
      for(int d=-4;d<=4;d++){
        auto Jf=[&](int node,int t)->i64{
            i64 ts;
            if(mode==0) ts = (i64)node*1024 + t + d;                 // no base subtraction at all
            else if(mode==1) ts = t + (t<0?1024:0) + d;              // base = floor node (distance from last LUT entry)
            else ts = t + d;                                          // centred base, off by d
            i64 dx=ts<<13; i64 I1=(arrN(KC,dx,ND_SQ1)+K)>>23; return (arrN(I1,dx,ND_SQ2)+K)>>18; };
        int g; int v=full_violations_nt(Jf,&g);
        const char* nm = mode==0?"t_sq = angle (no base sub)" : mode==1?"t_sq = angle mod 1024 (floor node)" : "t_sq = t (centred)";
        printf("  %-36s d=%+d  full-model violations %6d   guard(0,1,2,16) %5d\n", d==-4?nm:"", d, v, g);
      }
      printf("\n");
    }
    return 0; }
