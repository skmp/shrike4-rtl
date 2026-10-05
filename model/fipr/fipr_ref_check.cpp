#include <cstdio>
#include <vector>
#include "fipr_ref.h"
#include "fipr_vecgen.h"
#include "fipr_vecgen_edge.h"
int main() { uint32_t a[4], b[4]; unsigned long long bad = 0;
    { FILE* f = fopen("kos_fipr_dump/fipr_rnrz.bin", "rb"); std::vector<uint32_t> D((size_t)FIPR_N * 2); fread(D.data(), 8, FIPR_N, f); fclose(f); for (uint32_t i = 0; i < FIPR_N; i++) { fipr_vec(i, a, b); bad += fipr_ref(a, b, 0) != D[2*i]; bad += fipr_ref(a, b, 1) != D[2*i+1]; } printf("main dump: %llu mismatches\n", bad); }
    bad = 0; { FILE* f = fopen("kos_fipr_edge/fipr_edge_rnrz.bin", "rb"); std::vector<uint32_t> D((size_t)FIPRE_N * 2); fread(D.data(), 8, FIPRE_N, f); fclose(f); for (uint32_t i = 0; i < FIPRE_N; i++) { fipre_vec(i, a, b); bad += fipr_ref(a, b, 0) != D[2*i]; bad += fipr_ref(a, b, 1) != D[2*i+1]; } printf("edge dump: %llu mismatches\n", bad); }
    return 0; }
