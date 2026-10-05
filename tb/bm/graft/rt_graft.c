/* rt_graft.c -- the runtime pieces of a grafted console probe besides tb/bm/soc-design/bm_rt.c's printf / putchar /
 * fflush (tb/bm/graft, WI-SOC M3; README.md): the entry points graft.py redirects that bm_rt.c has no copy of.
 *   puts / fputs / fputc: to the sim-control putchar port (fputs / fputc ignore the stream: the probes only use stdout)
 *   malloc / memalign / calloc / free / realloc: the console's own buffers first -- bm_alloc_N, the addresses the
 *            console run printed (graft.py --alloc) --, then a bump allocator above the runtime, in P1 (never freed); the
 *            probes allocate a few buffers once
 *   abort / exit: the sim-control exit port with 0x1AB / the code
 *   timer_ms_gettime64 / timer_us_gettime64 / timer_ns_gettime64 / timer_ms_gettime / timer_spin_sleep: KOS's time
 *            from the sim-control time register (0xB8000008 / C) at BM_ICYC_PER_MS = 2000 Icyc per ms, the ports'
 *            convention (tb/bm/debug-ubc-hudi/bmrt.c): KOS's TMU is not started, and a probe's 2.2 s wait (ubcbrk's
 *            PMCTR ratio-mode read) becomes 4.4 M Icyc
 *   bm_kos_irq_init: KOS 2.1's irq_init() (kernel/arch/dreamcast/kernel/irq.c) step by step through KOS's own
 *            functions and tables in the image (graft.py --irq defines their addresses; KOS inlines irq_init into
 *            arch_main): the default TMU / TRAPA / FPU handlers, IPRC.DMAC = 3, the default register context, and
 *            VBR = KOS's vector table, so the probes' irq_set_handler() handlers run through KOS's dispatch */
#include <stddef.h>
#include <stdint.h>

#define SIM_PUTC (*(volatile uint32_t *)0xB8000004u)
#define SIM_EXIT (*(volatile uint32_t *)0xB8000000u)

int putchar(int c);

int puts(const char *s) {
    while (*s) putchar(*s++);
    putchar('\n');
    return 1;
}
int fputs(const char *s, void *f) {
    (void)f;
    while (*s) putchar(*s++);
    return 1;
}
int fputc(int c, void *f) { (void)f; return putchar(c); }

extern const uint32_t bm_allocs[];              /* graft.py: the console's buffer addresses, 0-terminated */
extern char bm_heap0[];
static unsigned nalloc;
static uintptr_t heap;
void *memalign(size_t align, size_t n) {
    if (bm_allocs[nalloc]) return (void *)(uintptr_t)bm_allocs[nalloc++];
    if (!heap) heap = ((uintptr_t)bm_heap0 & 0x1FFFFFFFu) | 0x80000000u;   /* P1, as KOS's malloc (runtime: P2) */
    if (align < 8) align = 8;
    heap = (heap + align - 1) & ~(uintptr_t)(align - 1);
    void *p = (void *)heap;
    heap += n;
    return heap > 0x8CF00000u ? 0 : p;                  /* keep 1 MB of stack below 0x8D000000 */
}
void *malloc(size_t n) { return memalign(32, n); }
void *calloc(size_t a, size_t b) {
    uint8_t *p = memalign(32, a * b);
    if (p) for (size_t i = 0; i < a * b; i++) p[i] = 0;
    return p;
}
void free(void *p) { (void)p; }
void *realloc(void *p, size_t n) {
    (void)p; (void)n;
    SIM_EXIT = 0x1AC;                                   /* not used by the probes */
    for (;;) {}
}
void abort(void) {
    SIM_EXIT = 0x1AB;
    for (;;) {}
}
void exit(int c) {
    SIM_EXIT = (uint32_t)c;
    for (;;) {}
}

typedef int (*kos_set_handler_t)(uint32_t code, void *hnd, void *data);
typedef void (*kos_set_prio_t)(int src, unsigned prio);
typedef void (*kos_set_ctx_t)(void *ctx);
extern char con_irq_set_handler[], con_irq_def_timer[], con_irq_handle_trapa[], con_trapa_handlers[],
    con_irq_def_fpu[], con_irq_set_priority[], con_irq_set_context[], con_irq_context_default[], con_irq_vma_table[];
void bm_kos_irq_init(void) {
    kos_set_handler_t sh = (kos_set_handler_t)(uintptr_t)con_irq_set_handler;
    sh(0x400, con_irq_def_timer, (void *)0);            /* EXC_TMU0_TUNI0, TMU0 */
    sh(0x420, con_irq_def_timer, (void *)1);            /* EXC_TMU1_TUNI1, TMU1 */
    sh(0x440, con_irq_def_timer, (void *)2);            /* EXC_TMU2_TUNI2, TMU2 */
    sh(0x160, con_irq_handle_trapa, con_trapa_handlers);  /* EXC_TRAPA */
    sh(0x120, con_irq_def_fpu, (void *)0);              /* EXC_FPU */
    ((kos_set_prio_t)(uintptr_t)con_irq_set_priority)(10, 3);   /* IRQ_SRC_DMAC, 3 */
    ((kos_set_ctx_t)(uintptr_t)con_irq_set_context)(con_irq_context_default);
    __asm__ volatile("ldc %0, vbr" : : "r"(con_irq_vma_table));
}

#define SIMC_TLO (*(volatile uint32_t *)0xB8000008u)
#define SIMC_THI (*(volatile uint32_t *)0xB800000Cu)
#define BM_ICYC_PER_MS 2000u
static uint64_t sim_icyc(void) {
    uint32_t hi, lo, hi2;
    do { hi = SIMC_THI; lo = SIMC_TLO; hi2 = SIMC_THI; } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}
uint64_t timer_ms_gettime64(void) { return sim_icyc() / BM_ICYC_PER_MS; }
uint64_t timer_us_gettime64(void) { return sim_icyc() * 1000u / BM_ICYC_PER_MS; }
uint64_t timer_ns_gettime64(void) { return sim_icyc() * 1000000u / BM_ICYC_PER_MS; }
void timer_ms_gettime(uint32_t *secs, uint32_t *msecs) {
    uint64_t ms = timer_ms_gettime64();
    if (secs) *secs = (uint32_t)(ms / 1000u);
    if (msecs) *msecs = (uint32_t)(ms % 1000u);
}
void timer_spin_sleep(int ms) {
    uint64_t t0 = timer_ms_gettime64();
    while (timer_ms_gettime64() - t0 < (uint64_t)ms) {}
}
