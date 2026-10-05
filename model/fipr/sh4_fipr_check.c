/* sh4_fipr_check.c -- sh4_fipr.h against every hardware capture of FIPR / FTRV in this tree.
 *   values : kos_fipr_dump/fipr_rnrz.bin (32M, DN=1), kos_fipr_edge/fipr_edge_rnrz.bin (5.2M edge, DN=1),
 *            hw_specials/fipr_dn0_rnrz.bin (1M, DN=0), hw_specials/ftrv_rnrz.bin (524,288 matrices)      -- RN and RZ each
 *   FPSCR  : hw_specials/fipr_specials.txt, ftrv_specials.txt, fpu_pr1_enables.txt, ftrv_infsign.txt  (result, FPSCR after, trap)
 * build: gcc -std=c99 -O2 -Wall -Wextra -pedantic -fopenmp sh4_fipr_check.c -o /tmp/fc && /tmp/fc      (HDR=... to test another header) */
#include <stdio.h>
#include <stdlib.h>
#ifndef HDR
#define HDR "sh4_fipr.h"
#endif
#include HDR
#include "fipr_vecgen.h"
#include "fipr_vecgen_edge.h"
#include "hw_specials/vecgen_dn0_ftrv.h"
#include "hw_specials/ftrv_specials_vectors.h"
static uint32_t *load(const char *p, size_t n) { uint32_t *b = malloc(4 * n); FILE *f = fopen(p, "rb"); if (!b || !f || fread(b, 4, n, f) != n) { printf("cannot read %s\n", p); exit(1); } fclose(f); return b; }
int main(void) { long total_bad = 0;
    { uint32_t *H = load("kos_fipr_dump/fipr_rnrz.bin", (size_t)FIPR_N * 2); long bad = 0; long long i;
      #pragma omp parallel for schedule(static) reduction(+:bad)
      for (i = 0; i < (long long)FIPR_N; i++) { uint32_t a[4], b[4]; fipr_vec((uint32_t)i, a, b); bad += sh4_fipr(a, b, 0) != H[2*i]; bad += sh4_fipr(a, b, 1) != H[2*i+1]; }
      printf("FIPR values, DN=1, %u vectors x RN/RZ        : %ld mismatches\n", (unsigned)FIPR_N, bad); total_bad += bad; free(H); }
    { uint32_t *H = load("kos_fipr_edge/fipr_edge_rnrz.bin", (size_t)FIPRE_N * 2); long bad = 0; long long i;
      #pragma omp parallel for schedule(static) reduction(+:bad)
      for (i = 0; i < (long long)FIPRE_N; i++) { uint32_t a[4], b[4]; fipre_vec((uint32_t)i, a, b); bad += sh4_fipr(a, b, 0) != H[2*i]; bad += sh4_fipr(a, b, 1) != H[2*i+1]; }
      printf("FIPR edge values, DN=1, %u vectors x RN/RZ    : %ld mismatches\n", (unsigned)FIPRE_N, bad); total_bad += bad; free(H); }
    { uint32_t *H = load("hw_specials/fipr_dn0_rnrz.bin", (size_t)DN0_N * 2); long bad = 0; long long i;
      #pragma omp parallel for schedule(static) reduction(+:bad)
      for (i = 0; i < (long long)DN0_N; i++) { uint32_t a[4], b[4], f, r; int rz; dn0_vec((uint32_t)i, a, b); for (rz = 0; rz < 2; rz++) { f = (uint32_t)rz; r = 0xdeadbeefu; if (sh4_fipr_ex(a, b, &f, &r) || r != H[2*i+rz]) bad++; } }
      printf("FIPR values, DN=0 (denormals), %u x RN/RZ     : %ld mismatches\n", (unsigned)DN0_N, bad); total_bad += bad; free(H); }
    { uint32_t *H = load("hw_specials/ftrv_rnrz.bin", (size_t)FTRV_N * 8); long bad = 0; long long i;
      #pragma omp parallel for schedule(static) reduction(+:bad)
      for (i = 0; i < (long long)FTRV_N; i++) { uint32_t m[16], v[4], w[4]; int rz, k; ftrv_vec((uint32_t)i, m, v); for (rz = 0; rz < 2; rz++) { for (k = 0; k < 4; k++) w[k] = v[k]; sh4_ftrv(m, w, rz); for (k = 0; k < 4; k++) bad += w[k] != H[8*i + 4*rz + k]; } }
      printf("FTRV values, %u matrices x 4 rows x RN/RZ       : %ld mismatches\n", (unsigned)FTRV_N, bad); total_bad += bad; free(H); }
    { FILE *f = fopen("hw_specials/fipr_specials.txt", "r"); char line[640]; int n = 0, bad = 0; if (!f) return 1;
      while (fgets(line, sizeof line, f)) { char lab[32]; unsigned cfg, v, a[4], b[4], res, after; int trap = strstr(line, "TRAP-") != NULL;
          if (sscanf(line, "%31s cfg=%x v=%u a=%x,%x,%x,%x b=%x,%x,%x,%x -> res=%x fpscr=%x", lab, &cfg, &v, &a[0], &a[1], &a[2], &a[3], &b[0], &b[1], &b[2], &b[3], &res, &after) != 13) continue;
          { uint32_t A[4], B[4], fp = cfg, r; int k, t; for (k = 0; k < 4; k++) { A[k] = a[k]; B[k] = b[k]; } r = B[3]; t = sh4_fipr_ex(A, B, &fp, &r); n++;
            if (r != res || fp != after || t != trap) { if (bad < 8) printf("  FIPR MISMATCH %s", line); bad++; } } }
      fclose(f); printf("FIPR FPSCR cases: %d, mismatches %d\n", n, bad); total_bad += bad; }
    { FILE *f = fopen("hw_specials/ftrv_specials.txt", "r"); char line[640]; int n = 0, bad = 0; if (!f) return 1;
      while (fgets(line, sizeof line, f)) { char lab[32]; unsigned cfg, ti, res[4], after; int trap = strstr(line, "TRAP-") != NULL;
          if (sscanf(line, "%31s cfg=%x t=%u -> res=%x,%x,%x,%x fpscr=%x", lab, &cfg, &ti, &res[0], &res[1], &res[2], &res[3], &after) != 8 || ti >= NT) continue;
          { uint32_t m[16], v[4], fp = cfg; int r, c, t; for (c = 0; c < 4; c++) for (r = 0; r < 4; r++) m[4*c + r] = TS[ti].row[r][c]; for (r = 0; r < 4; r++) v[r] = TS[ti].v[r]; t = sh4_ftrv_ex(m, v, &fp); n++;
            if (v[0] != res[0] || v[1] != res[1] || v[2] != res[2] || v[3] != res[3] || fp != after || t != trap) { if (bad < 8) printf("  FTRV MISMATCH %s", line); bad++; } } }
      fclose(f); printf("FTRV FPSCR cases: %d, mismatches %d\n", n, bad); total_bad += bad; }
    { FILE *f = fopen("hw_specials/fpu_pr1_enables.txt", "r"); char line[256]; int n = 0, bad = 0; if (!f) return 1;
      static const uint32_t IN[20] = { 0x3f800000u,0,0,0, 0,0x3f800000u,0,0, 0,0,0x3f800000u,0, 0,0,0,0x3f800000u,  0x3fc00000u,0x40000000u,0x40400000u,0x40800000u };
      while (fgets(line, sizeof line, f)) { char op[16]; unsigned pr, en, cfg, res, after; int trap = strstr(line, "TRAP-") != NULL;
          if (sscanf(line, "%15s PR=%u EN=%x cfg=%x -> res=%x fpscr=%x", op, &pr, &en, &cfg, &res, &after) != 6) continue;
          if (!strcmp(op, "FIPR")) { uint32_t fp = cfg, r = IN[19]; int t = sh4_fipr_ex(IN + 12, IN + 16, &fp, &r); n++; if (r != res || fp != after || t != trap) { printf("  MISMATCH %s", line); bad++; } }
          if (!strcmp(op, "FTRV")) { uint32_t fp = cfg, v[4]; int k, t; for (k = 0; k < 4; k++) v[k] = IN[16 + k]; t = sh4_ftrv_ex(IN, v, &fp); n++; if (v[1] != res || fp != after || t != trap) { printf("  MISMATCH %s", line); bad++; } } }
      fclose(f); printf("enable x PR table (FIPR, FTRV lines): %d, mismatches %d\n", n, bad); total_bad += bad; }
    { FILE *f = fopen("hw_specials/ftrv_infsign.txt", "r"); char line[400]; int n = 0, bad = 0; if (!f) return 1;
      while (fgets(line, sizeof line, f)) { int sgn, jn, j0, ninf, j; unsigned r, a, fr, fa;
          if (sscanf(line, "INFSIGN inf_sign=%d neg_finite_at=%d zero_at=%d (%d infinities) -> FTRV row0=%x fpscr=%x | FIPR(same operands, vector=FVm)=%x fpscr=%x", &sgn, &jn, &j0, &ninf, &r, &a, &fr, &fa) != 8) continue;
          { uint32_t in[16], v[4], rec[4], fp = 1u << 18, fp2 = 1u << 18, rr = 0; memset(in, 0, sizeof in);
            for (j = 0; j < 4; j++) { uint32_t m, x; if (j == jn) { m = 0xbf800000u; x = 0x40000000u; } else if (j == j0) { m = 0; x = 0x3f800000u; } else { m = 0x7f800000u | ((uint32_t)sgn << 31); x = 0x3f800000u; } in[4*j] = m; v[j] = x; rec[j] = m; }
            sh4_fipr_ex(v, rec, &fp2, &rr); n++; if (rr != fr || fp2 != fa) { printf("  MISMATCH (FIPR) %s", line); bad++; }
            sh4_ftrv_ex(in, v, &fp); n++; if (v[0] != r || fp != a) { printf("  MISMATCH (FTRV) %s", line); bad++; } } }
      fclose(f); printf("same-signed-infinities sign quirk, FTRV and FIPR: %d, mismatches %d\n", n, bad); total_bad += bad; }
    printf(total_bad ? "sh4_fipr_check: FAILED\n" : "sh4_fipr_check: OK\n"); return total_bad != 0; }
