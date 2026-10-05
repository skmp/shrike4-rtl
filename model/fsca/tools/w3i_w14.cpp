// w3i_w14.cpp -- the single best UNFITTED variant the branch-3 scan produced: the operand register carries
// one more bit of t (dx = t<<14) with the SAME stored constant KC and the shift split moved by +1 at each
// latch.  Full re-run: jwin18, the three pinned landmarks, and the full model over nodes 0..31 at both scales.
#include "w3_common.h"
static int WW=14, S1=24, S2=19;
static inline i64 Jw(int t){ i64 dx=(i64)t<<WW; i64 I1=(arr(KC,dx)+K)>>S1; return (arr(I1,dx)+K)>>S2; }
int main(){ if(!load_fsca(FD))return 1; build_tables(); if(!load_jwin())return 2;
    struct V{int w,s1,s2;} vs[]={{13,23,18},{14,24,19},{15,25,20},{16,26,21},{17,27,22},{18,28,23},{12,22,17},{16,26,20},{15,25,19}};
    printf("dx = t<<w, I1=(arr(KC,dx)+2^19)>>s1, J=(arr(I1,dx)+2^19)>>s2, KC=0x4EF145 UNCHANGED\n");
    printf(" w  s1  s2 | jwin18 | J(17)/45617 | J(342)/18466836 | J(-342)/[..58,..67] | fullFINE(guard)\n");
    for(auto&v:vs){ WW=v.w;S1=v.s1;S2=v.s2;
        auto Jf=[&](int t){ return Jw(t); };
        int sc=score_jwin(Jf,JLf,JHf); int g; DU=2; int fv=full_violations(Jf,&g);
        printf("%2d  %2d  %2d |  %4d  | %-11lld | %-15lld | %-19lld | %d(%d)%s\n", v.w,v.s1,v.s2,sc,
            (long long)Jf(17),(long long)Jf(342),(long long)Jf(-342),fv,g,
            (Jf(17)==45617||Jf(342)==18466836)?"  <== LANDMARK":"");
    }
    // how many bits of t reach the first multiply's truncated columns at each w?
    printf("\nbits of t reaching loss() at the first multiply (dx mod 2^16):\n");
    for(int w=10;w<=18;w++){ int nb = 16-w; if(nb<0)nb=0; if(nb>10)nb=10;
        printf("  w=%2d -> %2d bit(s) of t, %4d distinct loss inputs over 1024 offsets\n", w, nb, nb?(1<<nb):1); }
    return 0; }
