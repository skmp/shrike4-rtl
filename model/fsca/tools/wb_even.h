// wb_even.h -- shared machinery for the "what is the last multiply of the even path" experiments.
#pragma once
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
// generalised Booth array: nd digits (FIPR has 14 -> rec < 2^28).  Identical row/group rules.
static i64 arrN(i64 rec, i64 mul, int nd) { std::vector<i64> R(nd); i64 T = 0;
    for (int k = 0; k < nd; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k));
        T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2);
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }

static const i64 C0FIT[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2FIT[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
