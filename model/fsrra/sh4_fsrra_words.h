// sh4_fsrra_words.h -- the 34 FSRRA ROM words of the bit-exact structural model (tools/rr14_exact.cpp: 0 mismatches on all
// 16,777,216 inputs of [1,4), RZ and RN).  index = parity*17 + node.  Every field is pinned to its last bit by the dump
// (tools/rr15_unique.cpp: +-1 LSB costs 4K..260K inputs) except REC1 of the two one-sided end words, (0,0) [0x400000..3]
// and (1,16) [0x3ffff1..0x400000], where 0x400000 (slope exactly -1/2, the same field in both) is chosen.
//   C0   27-bit constant on the 2^-26 grid
//   REC1 23-bit NORMALISED linear field, E1 its 2-bit exponent   T1 = sm(arr(REC1, dx) + K, 26 - E1)
//   C2F  14-bit FIXED-POINT square field (no exponent)           T2 = sm(arr(C2F << 9, sq) + K, 29)
//   C3F   9-bit FIXED-POINT cubic field                          I  = (arr(C3F << 8, sq) + K) >> 23 ; T3 = sm(arr(dx, I) + K, 27)
//   dx = t << 4 ; sq = (arr(dx,dx) + K) >> 23 (ONE square latch) ; W = C0 - T1 + T2 - T3 ; K = 2^19
struct FsrraWord { int64_t C0; int32_t REC1; int E1; int32_t C2F; int32_t C3F; };
static const FsrraWord FSRRA_WORDS[34] = {
  {67108864LL, 0x400000, 2, 12296, 320},  // parity 0 node  0
  {65105160LL, 0x74dfa6, 1, 10566, 260},  // parity 0 node  1
  {63270842LL, 0x6b454d, 1,  9159, 212},  // parity 0 node  2
  {61583317LL, 0x62ea16, 1,  8000, 176},  // parity 0 node  3
  {60023991LL, 0x5b96df, 1,  7037, 148},  // parity 0 node  4
  {58577421LL, 0x552032, 1,  6228, 124},  // parity 0 node  5
  {57230630LL, 0x4f6360, 1,  5545, 106},  // parity 0 node  6
  {55972662LL, 0x4a4474, 1,  4962,  90},  // parity 0 node  7
  {54794160LL, 0x45aca4, 1,  4460,  78},  // parity 0 node  8
  {53687088LL, 0x418937, 1,  4029,  68},  // parity 0 node  9
  {52644524LL, 0x7b9555, 0,  3651,  58},  // parity 0 node 10
  {51660427LL, 0x74c814, 0,  3323,  52},  // parity 0 node 11
  {50729532LL, 0x6e94e4, 0,  3035,  46},  // parity 0 node 12
  {49847212LL, 0x68e955, 0,  2779,  40},  // parity 0 node 13
  {49009384LL, 0x63b5b8, 0,  2553,  36},  // parity 0 node 14
  {48212432LL, 0x5eec9f, 0,  2352,  32},  // parity 0 node 15
  {47453132LL, 0x5a8279, 0,  2174,  28},  // parity 0 node 16
  {47453132LL, 0x5a8279, 1,  8695, 226},  // parity 1 node  0
  {46036301LL, 0x52a463, 1,  7471, 183},  // parity 1 node  1
  {44739243LL, 0x4bda13, 1,  6476, 151},  // parity 1 node  2
  {43545981LL, 0x45f16b, 1,  5658, 124},  // parity 1 node  3
  {42443371LL, 0x40c371, 1,  4976, 104},  // parity 1 node  4
  {41420491LL, 0x7862d1, 0,  4404,  87},  // parity 1 node  5
  {40468166LL, 0x704598, 0,  3921,  74},  // parity 1 node  6
  {39578650LL, 0x6907ab, 0,  3508,  63},  // parity 1 node  7
  {38745321LL, 0x6288d1, 0,  3154,  55},  // parity 1 node  8
  {37962507LL, 0x5cae91, 0,  2847,  47},  // parity 1 node  9
  {37225301LL, 0x5762f9, 0,  2581,  41},  // parity 1 node 10
  {36529438LL, 0x5293b8, 0,  2350,  36},  // parity 1 node 11
  {35871196LL, 0x4e3168, 0,  2146,  32},  // parity 1 node 12
  {35247303LL, 0x4a2f01, 0,  1965,  28},  // parity 1 node 13
  {34654868LL, 0x468169, 0,  1805,  25},  // parity 1 node 14
  {34091336LL, 0x431f22, 0,  1664,  22},  // parity 1 node 15
  {33554432LL, 0x400000, 0,  1537,  20},  // parity 1 node 16
};
