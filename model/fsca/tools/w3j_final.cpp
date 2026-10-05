// w3j_final.cpp -- (1) the w=14 "extra bit" variant at the COARSE published scale, vs 148/147 baseline;
//                  (2) "t carries an extra LSB": dx = (2t+b)<<13, i.e. w=14 plus a half-LSB bias.
#include "w3_common.h"
int main(){ if(!load_fsca(FD))return 1; build_tables(); if(!load_jwin())return 2;
    printf("COARSE scale (u2 = 30+k, DU=0): baseline w=13 s1=23 s2=20 -> ");
    DU=0; { auto J=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
        int g; printf("%d violations, guard %d\n", full_violations(J,&g), g); }
    printf("COARSE scale: w=14 s1=24 s2=21 -> ");
    { auto J=[&](int t){ i64 dx=(i64)t<<14; i64 I1=(arr(KC,dx)+K)>>24; return (arr(I1,dx)+K)>>21; };
        int g; printf("%d violations, guard %d\n", full_violations(J,&g), g); }
    for(int w=15;w<=18;w++){ printf("COARSE scale: w=%d s1=%d s2=%d -> ",w,9+w,6+w);
        auto J=[&](int t){ i64 dx=(i64)t<<w; i64 I1=(arr(KC,dx)+K)>>(9+w); return (arr(I1,dx)+K)>>(6+w); };
        int g; printf("%d violations, guard %d\n", full_violations(J,&g), g); }
    DU=2;
    printf("\nFINE scale, 'an extra LSB on t': dx = (2t+b)<<13 = t<<14 + b*8192, s1=24 s2=19\n");
    printf("  b     jwin18   J(17)      J(342)     J(-342)    fullFINE(guard)\n");
    for(int b=-2;b<=2;b++){
        auto J=[&](int t){ i64 dx=((i64)t<<14)+(i64)b*8192; i64 I1=(arr(KC,dx)+K)>>24; return (arr(I1,dx)+K)>>19; };
        int g; int fv=full_violations(J,&g);
        printf("  %+d    %4d    %-10lld %-10lld %-10lld %d(%d)%s\n", b, score_jwin(J,JLf,JHf),
            (long long)J(17),(long long)J(342),(long long)J(-342),fv,g,
            (J(17)==45617||J(342)==18466836)?"  <== LANDMARK":"");
    }
    printf("\nFINE scale, same but the extra bit only for t<0 (sign-dependent half-LSB):\n");
    for(int b=-2;b<=2;b++){
        auto J=[&](int t){ i64 dx=((i64)t<<14)+(t<0?(i64)b*8192:0); i64 I1=(arr(KC,dx)+K)>>24; return (arr(I1,dx)+K)>>19; };
        int g; int fv=full_violations(J,&g);
        printf("  %+d    %4d    %-10lld %-10lld %-10lld %d(%d)%s\n", b, score_jwin(J,JLf,JHf),
            (long long)J(17),(long long)J(342),(long long)J(-342),fv,g,
            (J(17)==45617||J(342)==18466836)?"  <== LANDMARK":"");
    }
    return 0; }
