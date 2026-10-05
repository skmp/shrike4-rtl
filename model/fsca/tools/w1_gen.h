// w1_gen.h -- the FSCA structural model generalised over node count and split phase.
// The three coefficient rules are the SAME derived rules as base2.h; only the node angle and the
// datapath shifts move with the node size.  Derivation of the shift law (all w-independent by design):
//   dx = t << w ;  I1 = (arr(KC,dx)+K) >> (10+w) = KC*t/2^10 ;  J = (arr(I1,dx)+K) >> (7+w) = I1*t/2^7
//   A3 = ((arr(t*t<<3,dx)+K) >> (w+7)) >> 3 = t^3/2^7 ;  T1 shift = 10+w-e1
// so at w=13 this is exactly base2.h (23-e1, 23, 20, 20).  The FIPR array, K=2^19 and sm() are unchanged.
#pragma once
#include "base2.h"
#include <algorithm>
#include <vector>

struct GTab { i64 C1f; int e1; i64 c3; i64 S; int u2; i64 C0; i64 sin26; bool haveC; bool haveS; };

// derive one table entry for folded angle phi (in units of 1/65536 turn, 0..16384)
static GTab gderive(int phi, int w) {
    GTab T{}; i128 ang = PI_() * phi / 32768;          // phi/16384 * pi/2
    i128 c = cos_(ang), s = sin_(ang);
    if (phi == 16384) c = 0;                            // cos(pi/2) = 0 exactly
    if (phi == 0)     s = 0;
    // linear: 23-bit normalised field of cos*pi*2^(34-w) ; stored field is octave-normalised so it does
    // not depend on w -- only the shift does.
    if (c > 0) { i128 v = c*PI_()/QONE*((i128)1<<22); int e=0;
        while (v < ((i128)1<<23)*QONE) { v <<= 1; --e; }
        i128 q = v/(2*QONE), r = v%(2*QONE); if (r > QONE || (r == QONE && (q&1))) ++q;
        T.C1f = (i64)q; T.e1 = e; T.haveC = true; }
    else { T.C1f = 0; T.e1 = 0; T.haveC = false; }
    // cubic 12-bit: RN(cos * pi^3 * 512/6)
    if (c > 0) { i128 cu = c*PI_()/QONE; cu = cu*PI_()/QONE; cu = cu*PI_()/QONE; cu = cu*512/6;
        T.c3 = (i64)((cu + QONE/2)/QONE); } else T.c3 = 0;
    // square's table sine: 23-bit normalised field + u2 = 30 + k
    if (s > 0) { int k = 0; while (s*((i128)1<<(k+1)) < QONE) k++;
        i64 sf = rn_scale(s, 23+k); if (sf >= (1ll<<23)) sf = (1ll<<23)-1;
        T.S = sf; T.u2 = 30+k; T.haveS = true; }
    else { T.S = 0; T.u2 = 30; T.haveS = false; }
    i64 c026 = rn_scale(s, 26); if (c026 >= (1ll<<26)) c026 = (1ll<<26)-1;
    T.sin26 = c026; T.C0 = c026;
    return T; }

// the model value at offset t for a table entry, with C0/u2 overridden and sign flip
static inline i64 gV(const GTab& T, i64 t, int flip, i64 C0, int u2, int w, i64 KCc) {
    i64 dx = t << w;
    i64 T1 = T.haveC ? sm(arr(T.C1f, dx) + K, 10 + w - T.e1) : 0;
    i64 A3 = ((arr((t*t) << 3, dx) + K) >> (w + 7)) >> 3;
    i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
    i64 I1 = (arr(KCc, dx) + K) >> (10 + w);
    i64 J  = (arr(I1, dx) + K) >> (7 + w);
    i64 T2 = T.haveS ? sm(arr(J, T.S) + K, u2) : 0;
    i64 s = flip ? -1 : 1;
    return C0 + s*T1 - T2 - s*T3; }
