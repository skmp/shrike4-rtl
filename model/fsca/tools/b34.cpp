// b34.cpp -- HISTORICAL (emits the pre-exact structural model with a separate sine field; output now goes to the archive
// so it cannot shadow ../sh4_fsca.h).  emit the structural reference model as ../archive/fsca_ref2_from_b34.h and measure it against the dump
// (float mismatches in both rounding modes, and window violations).
#include "base2.h"
#include <cstring>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 Vof(int m,int t){ int neg=0; if(m>=33){m-=32;neg=1;} int flip=0; if(m>=17){m=32-m;flip=1;}
    i64 dx=(i64)t<<13; i64 s=flip?-1:1;
    i64 T1=TB[m].C1f? sm(arr(TB[m].C1f,dx)+K,23-TB[m].e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[m].c3? sm(arr(TB[m].c3<<5,A3)+K,26):0;
    i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;          // verified tie-break: dx is the recoded operand
    i64 T2=TB[m].S? sm(arr(J,TB[m].S)+K,U2TAB[m]):0;
    i64 V=C0TAB[m]+s*T1-T2-s*T3; return neg?-V:V; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    int rn=0,rz=0,wv=0;
    for(int a=0;a<65536;a++){ int q=(a+512)>>10, m=q&63, t=a-1024*q; if(t<-512)t+=65536;
        i64 V=Vof(m,t); if(to_float(V,0)!=F.srn[a])rn++; if(to_float(V,1)!=F.srz[a])rz++;
        i64 lo,hi;bool ex; win_of(a,lo,hi,ex); if(V<lo||V>hi)wv++; }
    printf("structural model over the whole dump: RN %d / RZ %d float mismatches, %d window violations (of 65536)\n",rn,rz,wv);
    printf("  (old fsca_ref.h: 480 RN / 429 RZ, 830 window violations)\n");
    // emit the header
    FILE*f=fopen("../archive/fsca_ref2_from_b34.h","w");
    fprintf(f,"/* SH7091 FSCA structural reference model, 2026-09-18.  NOT bit-exact: %d RN / %d RZ float\n"
              " * mismatches per 65536 sine angles (old fsca_ref.h: 480 / 429), %d window violations (old: 830).\n"
              " * Unlike the old model this has NO fitted per-node constants: every coefficient is a stated rule.\n"
              " *   angle = FPUL & 0xffff ; node = (angle + 512) >> 10 (64 nodes) ; t = angle - 1024*node in [-512,511]\n"
              " *   dx = t << 13 (signed 24-bit multiplicand) ; arr() = the FIPR Booth array ; K = 2^19\n"
              " *   sm(P,u) = P>=0 ? P>>u : -((~P)>>u)   (sign-magnitude by one's complement, then truncate)\n"
              " *   V = C0 + s*T1 - T2 - s*T3   on the 2^-26 grid ; s = -1 for nodes 17..31 (cos changes sign)\n"
              " *   T1 = sm(arr(C1f, dx) + K, 23 - e1)          C1f = RN23(cos(n pi/32) * pi/2 * 2^23) field\n"
              " *   A3 = ((arr(t*t<<3, dx) + K) >> 20) >> 3\n"
              " *   T3 = sm(arr(c3 << 5, A3) + K, 26)           c3  = RN(cos(n pi/32) * 2646), 12 bit\n"
              " *   I1 = (arr(dx, KC) + K) >> 23 ; J = (arr(I1, dx) + K) >> 20 ; KC = 0x4EF145 (economised 2pi^2)\n"
              " *   T2 = sm(arr(J, S) + K, u2)                  S = RN23(sin(n pi/32)) field, u2 = 30 + k\n"
              " *   C0 = the 26-bit table value (hardware's value at t = 0; generation rule still unknown)\n"
              " * The residual is entirely in J: giving J a free value per t drops the model to 2 mismatching\n"
              " * samples of 32768.  See NOTES.md 2026-09-18 and tools/jwin.txt. */\n",rn,rz,wv);
    fprintf(f,"#ifndef FSCA_REF2_H\n#define FSCA_REF2_H\n#include <stdint.h>\n");
    fprintf(f,"static const int64_t FSCA2_C1F[17] = {"); for(int n=0;n<=16;n++)fprintf(f,"0x%llx%s",(long long)TB[n].C1f,n<16?",":"};\n");
    fprintf(f,"static const int      FSCA2_E1[17] = {"); for(int n=0;n<=16;n++)fprintf(f,"%d%s",TB[n].e1,n<16?",":"};\n");
    fprintf(f,"static const int64_t  FSCA2_C3[17] = {"); for(int n=0;n<=16;n++)fprintf(f,"0x%llx%s",(long long)TB[n].c3,n<16?",":"};\n");
    fprintf(f,"static const int64_t   FSCA2_S[17] = {"); for(int n=0;n<=16;n++)fprintf(f,"0x%llx%s",(long long)TB[n].S,n<16?",":"};\n");
    fprintf(f,"static const int      FSCA2_U2[17] = {"); for(int n=0;n<=16;n++)fprintf(f,"%d%s",U2TAB[n],n<16?",":"};\n");
    fprintf(f,"static const int64_t  FSCA2_C0[17] = {"); for(int n=0;n<=16;n++)fprintf(f,"%lld%s",(long long)C0TAB[n],n<16?",":"};\n");
    fprintf(f,"#define FSCA2_KC 0x4ef145LL\n#define FSCA2_K  (1LL<<19)\n");
    fprintf(f,"static int64_t fsca2_arr(int64_t rec, int64_t mul) { int64_t R[14], T = 0; int k;\n"
              "    for (k = 0; k < 14; k++) { int b1 = 2*k+1 > 62 ? 62 : 2*k+1, b0 = 2*k > 62 ? 62 : 2*k, bm = 2*k-1;\n"
              "        int d = -2*(int)((rec >> b1) & 1) + (int)((rec >> b0) & 1) + (bm < 0 ? 0 : (int)((rec >> bm) & 1));\n"
              "        int64_t mag = (int64_t)(d < 0 ? -d : d) * mul, row = d < 0 ? ~mag : mag;\n"
              "        R[k] = row * (1LL << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1LL << (2*k); }\n"
              "    { int m15 = (int)((R[0]>>15)&1) + (int)((R[1]>>15)&1) + (int)((R[2]>>15)&1);\n"
              "      int p16 = (int)(((R[6]>>16) ^ (R[7]>>16) ^ (R[8]>>16)) & 1);\n"
              "      T += (1LL<<16) * (m15 >= 2); T -= (1LL<<16) * p16; } return T; }\n");
    fprintf(f,"static int64_t fsca2_sm(int64_t P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }\n");
    fprintf(f,"static int64_t fsca2_internal(uint32_t angle16) {\n"
              "    int q = (int)((angle16 + 512u) >> 10), m = q & 63, t = (int)angle16 - 1024*q, neg = 0, flip = 0;\n"
              "    int64_t dx, T1, A3, T3, I1, J, T2, s, V;\n"
              "    if (t < -512) t += 65536;\n"
              "    if (m >= 33) { m -= 32; neg = 1; } if (m >= 17) { m = 32 - m; flip = 1; }\n"
              "    dx = (int64_t)t << 13; s = flip ? -1 : 1;\n"
              "    T1 = FSCA2_C1F[m] ? fsca2_sm(fsca2_arr(FSCA2_C1F[m], dx) + FSCA2_K, 23 - FSCA2_E1[m]) : 0;\n"
              "    A3 = ((fsca2_arr(((int64_t)t*t) << 3, dx) + FSCA2_K) >> 20) >> 3;\n"
              "    T3 = FSCA2_C3[m] ? fsca2_sm(fsca2_arr(FSCA2_C3[m] << 5, A3) + FSCA2_K, 26) : 0;\n"
              "    I1 = (fsca2_arr(dx, FSCA2_KC) + FSCA2_K) >> 23; J = (fsca2_arr(I1, dx) + FSCA2_K) >> 20;\n"
              "    T2 = FSCA2_S[m] ? fsca2_sm(fsca2_arr(J, FSCA2_S[m]) + FSCA2_K, FSCA2_U2[m]) : 0;\n"
              "    V = FSCA2_C0[m] + s*T1 - T2 - s*T3; return neg ? -V : V; }\n");
    fprintf(f,"static uint32_t fsca2_to_float(int64_t V, int rz) { uint32_t sign = V < 0; uint64_t m = (uint64_t)(V<0?-V:V), q, r, h;\n"
              "    int p, sh; if (!m) return sign << 31; p = 63 - __builtin_clzll(m); sh = p - 23;\n"
              "    if (sh <= 0) q = m << (-sh); else { r = m & ((1ULL<<sh)-1); h = 1ULL << (sh-1); q = m >> sh;\n"
              "        if (!rz) q += (r > h) || (r == h && (q & 1)); if (q >> 24) { q >>= 1; p++; } }\n"
              "    return (sign << 31) | ((uint32_t)(127 - 26 + p) << 23) | ((uint32_t)q & 0x7fffff); }\n");
    fprintf(f,"static uint32_t fsca2_sin(uint32_t fpul, int rz) { return fsca2_to_float(fsca2_internal(fpul & 0xffffu), rz); }\n"
              "static uint32_t fsca2_cos(uint32_t fpul, int rz) { return fsca2_to_float(fsca2_internal((fpul + 0x4000u) & 0xffffu), rz); }\n"
              "#endif\n");
    fclose(f); printf("wrote ../archive/fsca_ref2_from_b34.h\n"); return 0; }
