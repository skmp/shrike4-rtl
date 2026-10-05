/* t2run.c -- console side of the pairwise SH-4 pipeline sweep (see ../README.md).
 *
 * Reads ../tests/chunk_NNNNN.bin over dcload (/pc), and for every case and every distance n JIT-assembles
 *     L: A ; add #1,r14 x n ; B ; add #1,r14 x DRAIN ; restores ; dt r13 ; bf/s L ; <T fix> ; -> jit_exit
 * into jit_code, runs it N1 and N2 times (interrupts masked, a full register image loaded by jit_enter), REPS times
 * each, and records min(N2 run) - min(N1 run) of PRFC1 (CPU cycles) and of PRFC0 (the event run.cfg selects), i.e.
 * the exact cost of N2 - N1 iterations with every fixed overhead cancelled.  Results go to
 * ../results/<pass>/chunk_NNNNN.bin; a chunk whose result file exists is skipped, so a run can be resumed.
 * With no test chunk present it only writes ../probe.txt (buffer addresses, SR and VBR while timing), which
 * gen.py needs.  Any exception inside a case is caught, sends the case to jit_exit and is recorded in its status. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <arch/cache.h>
#include <dc/perfctr.h>

#define BASE "/pc" SHRIKE4_ROOT "/model/timmings2/"
#define POOL_OFF 384            /* bytes from jit_code */
#define POOL_SIZE 64
#define SCRATCH_SIZE 512
#define HARD_OFF 32
#define P_DEF_OFF 160

extern void jit_enter(uint32_t *state);
extern void jit_exit(void);
extern uint32_t jit_save[];

/* scratch and code each start their own 4 KB block with unused space around them, so an access that strays from
 * the scratch area cannot reach the code or the pool */
uint8_t guard0[4096] __attribute__((aligned(4096)));
uint8_t scratch[SCRATCH_SIZE] __attribute__((aligned(4096)));
uint8_t guard1[4096] __attribute__((aligned(4096)));
uint16_t jit_code[512] __attribute__((aligned(4096)));
static uint8_t scratch_img[SCRATCH_SIZE] __attribute__((aligned(32)));
static uint32_t state[80], state_def[80];
static uint32_t sr_run, vbr_run;
static volatile uint32_t g_exc, g_exc_pc;

#define PMCTR_LO(c) (*(volatile uint32_t *)(0xFF100008 + 8 * (c)))
#define PTEL (*(volatile uint32_t *)0xFF000004)

/* the exceptions a case could raise by mistake.  Not TRAPA, FPU or user break: KOS installs its own handlers for
 * those (irq.c, ubc.c).  Installed only while a chunk is timed; the previous handlers are put back after it. */
static const irq_t excs[] = { EXC_ILLEGAL_INSTR, EXC_SLOT_ILLEGAL_INSTR, EXC_GENERAL_FPU, EXC_SLOT_FPU,
                              EXC_DATA_ADDRESS_READ, EXC_DATA_ADDRESS_WRITE };
#define NEXC (sizeof excs / sizeof excs[0])
static irq_cb_t old_cb[NEXC];

static void on_exc(irq_t code, irq_context_t *ctx, void *data);
static void net_install(void) {
    for (unsigned k = 0; k < NEXC; k++) { old_cb[k] = irq_get_handler(excs[k]); irq_set_handler(excs[k], on_exc, NULL); }
}
static void net_remove(void) {
    for (unsigned k = 0; k < NEXC; k++) irq_set_handler(excs[k], old_cb[k].hdl, old_cb[k].data);
}

static void on_exc(irq_t code, irq_context_t *ctx, void *data) {
    (void)data;
    g_exc = code ? code : 1; g_exc_pc = ctx->pc;
    ctx->pc = (uint32_t)jit_exit;
    ctx->sr = jit_save[16];            /* the SR jit_enter saved: RB = 0, BL = 0, IMASK 15 */
}

static uint32_t rd_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd_u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

static uint8_t *load_file(const char *path, size_t *len) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) return NULL;
    size_t sz = fs_total(f);
    uint8_t *buf = malloc(sz ? sz : 1);
    if (!buf) { fs_close(f); return NULL; }
    size_t got = 0;
    while (got < sz) { ssize_t r = fs_read(f, buf + got, sz - got); if (r <= 0) break; got += (size_t)r; }
    fs_close(f);
    if (got != sz) { free(buf); return NULL; }
    *len = sz;
    return buf;
}

static int exists(const char *path) { file_t f = fs_open(path, O_RDONLY); if (f < 0) return 0; fs_close(f); return 1; }

/* configuration (run.cfg: "pass=<name> event=<hex> n1=<> n2=<> reps=<>") */
static char pass_name[32] = "p0";
static uint32_t cfg_event = 0x14, cfg_n1 = 8, cfg_n2 = 136, cfg_reps = 3, cfg_verbose = 0, cfg_stop = 0, cfg_bracket = 0;

static void read_cfg(void) {
    size_t len; uint8_t *b = load_file(BASE "run.cfg", &len);
    if (!b) return;
    char *s = malloc(len + 1); memcpy(s, b, len); s[len] = 0; free(b);
    for (char *tok = strtok(s, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        char *eq = strchr(tok, '='); if (!eq) continue; *eq = 0; const char *v = eq + 1;
        if (!strcmp(tok, "pass")) { strncpy(pass_name, v, sizeof pass_name - 1); }
        else if (!strcmp(tok, "event")) cfg_event = strtoul(v, NULL, 16);
        else if (!strcmp(tok, "n1")) cfg_n1 = strtoul(v, NULL, 10);
        else if (!strcmp(tok, "n2")) cfg_n2 = strtoul(v, NULL, 10);
        else if (!strcmp(tok, "reps")) cfg_reps = strtoul(v, NULL, 10);
        else if (!strcmp(tok, "verbose")) cfg_verbose = strtoul(v, NULL, 10);
        else if (!strcmp(tok, "bracket")) cfg_bracket = strtoul(v, NULL, 16);  /* replaces the first pad after B */
        else if (!strcmp(tok, "stop")) cfg_stop = strtoul(v, NULL, 10);     /* stop after this many chunks */
    }
    free(s);
}

static void set_defaults(void) {
    memset(state_def, 0, sizeof state_def);
    for (int k = 0; k <= 10; k++) state_def[k] = 0x1000 + 0x10 * k;
    state_def[11] = 0; state_def[12] = (uint32_t)scratch;
    state_def[15] = sr_run;
    state_def[16] = (uint32_t)scratch + HARD_OFF;    /* GBR */
    state_def[17] = sr_run;                          /* SSR */
    state_def[23] = 0x3F800000;                      /* FPUL */
    state_def[22] = 0x3000;                          /* PR */
    for (int k = 0; k < 8; k++) state_def[25 + k] = 0x2000 + 0x10 * k;
    for (int k = 0; k < 16; k++) { state_def[33 + k] = 0x3F800000; state_def[49 + k] = (k % 5 == 0) ? 0x3F800000 : 0; }
}

/* init register id (gen.py REGID) -> state word */
static int state_index(unsigned rid) {
    if (rid <= 12) return (int)rid;
    switch (rid) {
    case 16: return 15; case 17: return 16; case 18: return 17; case 19: return 18; case 20: return 19;
    case 21: return 20; case 22: return 21; case 23: return 22; case 24: return 23;
    }
    if (rid >= 32 && rid < 40) return 25 + (int)(rid - 32);
    if (rid >= 64 && rid < 80) return 33 + (int)(rid - 64);
    if (rid >= 80 && rid < 96) return 49 + (int)(rid - 80);
    return -1;
}

typedef struct { uint32_t id, fpscr; uint16_t tfix; uint8_t align, nn; const uint8_t *ns;
                 uint8_t nrest, ninit, nmem; const uint8_t *code, *init, *mem; } case_t;

static const uint8_t *parse_case(const uint8_t *p, case_t *c) {
    uint16_t len = rd_u16(p);
    const uint8_t *q = p + 4;
    c->id = rd_u32(q); c->fpscr = rd_u32(q + 4); c->tfix = rd_u16(q + 8); c->align = q[10]; c->nn = q[11]; q += 12;
    c->ns = q; q += (c->nn + 3) & ~3;
    c->nrest = q[0]; c->ninit = q[1]; c->nmem = q[2]; q += 4;
    c->code = q; q += ((2 + c->nrest) * 2 + 3) & ~3;
    c->init = q; q += 8 * c->ninit;
    c->mem = q; q += 8 * c->nmem;
    return p + len;
}

#define DRAIN_MAX 64
static uint32_t g_drain = 40;

/* returns the entry address L */
static uint32_t jit_build(const case_t *c, unsigned n) {
    uint16_t *code = jit_code;
    unsigned L = c->align ? 1 : 0, i = L;
    if (c->align) code[0] = 0x0009;
    code[i++] = rd_u16(c->code);
    for (unsigned k = 0; k < n; k++) code[i++] = 0x7E01;
    code[i++] = rd_u16(c->code + 2);
    for (unsigned k = 0; k < g_drain; k++) code[i++] = (k == 0 && cfg_bracket) ? (uint16_t)cfg_bracket : 0x7E01;
    for (unsigned k = 0; k < c->nrest; k++) code[i++] = rd_u16(c->code + 4 + 2 * k);
    code[i++] = 0x4D10;                                   /* dt r13 */
    int disp = ((int)L * 2 - ((int)i * 2 + 4)) / 2;
    code[i++] = 0x8F00 | (disp & 0xFF);                   /* bf/s L */
    code[i++] = c->tfix;                                  /* sett / nop */
    uint32_t pc = (uint32_t)&code[i];
    uint32_t pool = (uint32_t)jit_code + POOL_OFF;
    code[i++] = 0xDD00 | (((pool - ((pc & ~3u) + 4)) / 4) & 0xFF);   /* mov.l @(k,PC),r13 */
    code[i++] = 0x4D2B;                                   /* jmp @r13 */
    code[i++] = 0x0009;
    return (uint32_t)jit_code + 2 * L;
}

static void pool_build(const case_t *c) {
    uint32_t *pool = (uint32_t *)((uint8_t *)jit_code + POOL_OFF);
    pool[0] = (uint32_t)jit_exit;
    for (int k = 1; k < POOL_SIZE / 4; k++) pool[k] = (uint32_t)scratch + P_DEF_OFF;
    const uint8_t *m = c->mem;
    for (unsigned k = 0; k < c->nmem; k++, m += 8)
        if (m[0] == 1) memcpy((uint8_t *)pool + rd_u16(m + 2), m + 4, 4);
}

static void scratch_build(const case_t *c) {
    memset(scratch_img, 0, HARD_OFF);
    for (int off = HARD_OFF; off < SCRATCH_SIZE; off += 4) { uint32_t v = (uint32_t)scratch + P_DEF_OFF; memcpy(scratch_img + off, &v, 4); }
    const uint8_t *m = c->mem;
    for (unsigned k = 0; k < c->nmem; k++, m += 8)
        if (m[0] == 0) memcpy(scratch_img + rd_u16(m + 2), m + 4, 4);
}

static void state_build(const case_t *c, uint32_t entry) {
    memcpy(state, state_def, sizeof state);
    const uint8_t *p = c->init;
    for (unsigned k = 0; k < c->ninit; k++, p += 8) { int ix = state_index(p[0]); if (ix >= 0) state[ix] = rd_u32(p + 4); }
    state[14] = entry;
    state[24] = c->fpscr;
}

static int run_once(uint32_t iters, uint32_t *cyc, uint32_t *ev) {
    state[13] = iters;
    memcpy(scratch, scratch_img, SCRATCH_SIZE);
    dcache_flush_range((uintptr_t)scratch, SCRATCH_SIZE);
    g_exc = 0;
    irq_mask_t im = irq_disable();
    uint32_t c0 = PMCTR_LO(1), e0 = PMCTR_LO(0);
    jit_enter(state);
    uint32_t c1 = PMCTR_LO(1), e1 = PMCTR_LO(0);
    irq_restore(im);
    *cyc = c1 - c0; *ev = e1 - e0;
    return g_exc ? -1 : 0;
}

/* one distance: returns 0 or the exception code */
static uint32_t measure(const case_t *c, unsigned n, uint32_t *dcyc, uint32_t *dev) {
    uint32_t entry = jit_build(c, n);
    pool_build(c);
    dcache_flush_range((uintptr_t)jit_code, sizeof jit_code);
    icache_flush_range((uintptr_t)jit_code, sizeof jit_code);
    state_build(c, entry);
    uint32_t cy, e, best1 = ~0u, best2 = ~0u, ev1 = 0, ev2 = 0;
    if (run_once(cfg_n1, &cy, &e)) return g_exc;          /* warm-up */
    for (uint32_t r = 0; r < cfg_reps; r++) {
        if (run_once(cfg_n1, &cy, &e)) return g_exc;
        if (cy < best1) { best1 = cy; ev1 = e; }
        if (run_once(cfg_n2, &cy, &e)) return g_exc;
        if (cy < best2) { best2 = cy; ev2 = e; }
    }
    *dcyc = best2 - best1; *dev = ev2 - ev1;
    return 0;
}

static int write_file(const char *path, const void *buf, size_t len) {
    file_t f = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (f < 0) return -1;
    ssize_t w = fs_write(f, buf, len);
    fs_close(f);
    return w == (ssize_t)len ? 0 : -1;
}

static void probe(void) {
    char buf[256];
    int k = snprintf(buf, sizeof buf, "scratch=%08lx\ncode=%08lx\nexit=%08lx\nsr=%08lx\nvbr=%08lx\n",
                     (unsigned long)(uint32_t)scratch, (unsigned long)(uint32_t)jit_code, (unsigned long)(uint32_t)jit_exit,
                     (unsigned long)sr_run, (unsigned long)vbr_run);
    printf("%s", buf);
    write_file(BASE "probe.txt", buf, (size_t)k);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    perf_cntr_timer_disable();
    { irq_mask_t im = irq_disable(); uint32_t s, v; __asm__ volatile("stc sr,%0\n\tstc vbr,%1" : "=r"(s), "=r"(v)); irq_restore(im);
      sr_run = s & ~0x303u; vbr_run = v; }
    read_cfg();
    set_defaults();
    printf("t2run: pass %s event %02lx n1 %lu n2 %lu reps %lu bracket %04lx\n", pass_name, (unsigned long)cfg_event, (unsigned long)cfg_n1,
           (unsigned long)cfg_n2, (unsigned long)cfg_reps, (unsigned long)cfg_bracket);
    probe();
    if (!exists(BASE "tests/chunk_00000.bin")) { printf("no tests: probe only\nDONE\n"); return 0; }
    uint32_t ptel = PTEL; PTEL = 0;                        /* LDTLB writes an invalid entry (the MMU is off anyway) */
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    perf_cntr_start(PRFC0, (perf_cntr_event_t)cfg_event, PMCR_COUNT_CPU_CYCLES);
    char path[256], rpath[256];
    snprintf(rpath, sizeof rpath, BASE "results/%s", pass_name);
    fs_mkdir(BASE "results");
    fs_mkdir(rpath);
    uint64_t t_start = timer_ms_gettime64();
    unsigned total_cases = 0, total_exc = 0;
    for (unsigned ch = 0; !cfg_stop || ch < cfg_stop; ch++) {
        snprintf(path, sizeof path, BASE "tests/chunk_%05u.bin", ch);
        snprintf(rpath, sizeof rpath, BASE "results/%s/chunk_%05u.bin", pass_name, ch);
        if (!exists(path)) break;
        if (exists(rpath)) continue;
        size_t len; uint8_t *buf = load_file(path, &len);
        if (!buf || len < 40 || memcmp(buf, "T2CH", 4)) { printf("bad chunk %u\n", ch); free(buf); break; }
        uint32_t ncase = rd_u32(buf + 8);
        if (rd_u32(buf + 12) != (uint32_t)scratch || rd_u32(buf + 16) != (uint32_t)jit_code ||
            rd_u32(buf + 20) != (uint32_t)jit_exit || rd_u32(buf + 24) != sr_run || rd_u32(buf + 28) != vbr_run) {
            printf("chunk %u was generated for another layout (rerun gen.py with the new probe.txt)\n", ch);
            free(buf); break;
        }
        g_drain = rd_u32(buf + 32);
        uint8_t *out = malloc(16 + (size_t)ncase * (8 + 8 * 32));
        uint8_t *o = out + 16;
        memcpy(out, "T2RS", 4); memcpy(out + 4, &ncase, 4); memcpy(out + 8, &cfg_event, 4);
        { uint32_t d = cfg_n2 - cfg_n1; memcpy(out + 12, &d, 4); }
        const uint8_t *p = buf + 40;
        unsigned nexc = 0;
        net_install();
        for (uint32_t k = 0; k < ncase; k++) {
            case_t c; p = parse_case(p, &c);
            scratch_build(&c);
            uint32_t status = 0;
            memcpy(o, &c.id, 4); o[6] = c.nn; o[7] = 0;
            uint8_t *res = o + 8;
            for (unsigned j = 0; j < c.nn; j++) {
                uint32_t dc = 0, de = 0;
                if (!status) {
                    if (cfg_verbose) printf("case %lu n %u\n", (unsigned long)c.id, c.ns[j]);
                    uint32_t e = measure(&c, c.ns[j], &dc, &de);
                    if (cfg_verbose > 1) printf("  -> %lu %lu exc %lx\n", (unsigned long)dc, (unsigned long)de, (unsigned long)e);
                    if (e) { status = (e & 0xFFF) | ((uint32_t)j << 12); dc = g_exc_pc; nexc++; }
                }
                memcpy(res + 8 * j, &dc, 4); memcpy(res + 8 * j + 4, &de, 4);
            }
            { uint16_t s16 = (uint16_t)status; memcpy(o + 4, &s16, 2); }
            o = res + 8 * c.nn;
        }
        net_remove();
        write_file(rpath, out, (size_t)(o - out));
        free(out); free(buf);
        total_cases += ncase; total_exc += nexc;
        printf("chunk %u: %lu cases, %u exceptions, %lu s\n", ch, (unsigned long)ncase, nexc,
               (unsigned long)((timer_ms_gettime64() - t_start) / 1000));
    }
    PTEL = ptel;
    perf_cntr_stop(PRFC0); perf_cntr_stop(PRFC1);
    perf_cntr_timer_enable();                             /* KOS's high-resolution timer runs on PRFC0 */
    printf("t2run: %u cases, %u with exceptions\nDONE\n", total_cases, total_exc);
    return 0;
}
