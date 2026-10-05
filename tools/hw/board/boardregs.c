/* boardregs.c -- Dreamcast board facts for rtl/v1/spec/board.md (one console run, read-mostly).
 *
 *  1. "pre":  Holly system-bus / PVR / GD-ROM / AICA registers and the SH7091 on-chip registers as dcload (and the
 *             BIOS before it) left them, snapshotted from KOS_INIT_EARLY (before KOS touches any hardware except
 *             CCR / SR / FPSCR / WDT / UBC).
 *  2. "post": the same list after KOS INIT_DEFAULT (what KOS programs).
 *  3. SPG:    ISTNRM vblank-in / vblank-out / hblank event times (PMCTR0 CPU cycles) and SPG_STATUS scanline
 *             progression, IRQs masked, for ~70 ms.
 *  4. Latency: CPU cycles per uncached access to each board target, per SQ burst, per cache-line miss.
 *  5. G2 FIFO: SB_FFST after 8 PIO longword writes to unused AICA RAM, sampled until it drains.
 *  6. RAM / VRAM mirrors (read only), cable-type / PDTRA, GD-ROM drive status via the syscall.
 * Nothing is left changed: ISTNRM bits 3..5 are acked (KOS would have), QACR0/1 are restored, the AICA RAM / VRAM
 * words written are outside anything in use (AICA 0x00810000.., VRAM 0x00700000..).  IRQs are masked only around
 * bounded loops.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/cdrom.h>
#include <dc/g2bus.h>

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))
#define CYC    R32(0xFF100008)            /* PMCTR0 low: CPU cycles (KOS perf_cntr_timer_enable) */

typedef struct { const char *name; uint32_t addr; uint8_t w; } reg_t;

static const reg_t regs[] = {
    /* ---- Holly system bus (SB) */
    {"SB_C2DSTAT",0xA05F6800,4},{"SB_C2DLEN",0xA05F6804,4},{"SB_C2DST",0xA05F6808,4},{"SB_SDSTAW",0xA05F6810,4},
    {"SB_SDBAAW",0xA05F6814,4},{"SB_SDWLT",0xA05F6818,4},{"SB_SDLAS",0xA05F681C,4},{"SB_SDST",0xA05F6820,4},
    {"SB_DBREQM",0xA05F6840,4},{"SB_BAVLWC",0xA05F6844,4},{"SB_C2DPRYC",0xA05F6848,4},{"SB_C2DMAXL",0xA05F684C,4},
    {"SB_TFREM",0xA05F6880,4},{"SB_LMMODE0",0xA05F6884,4},{"SB_LMMODE1",0xA05F6888,4},{"SB_FFST",0xA05F688C,4},
    {"SB_SBREV",0xA05F689C,4},{"SB_RBSPLT",0xA05F68A0,4},
    {"SB_ISTNRM",0xA05F6900,4},{"SB_ISTEXT",0xA05F6904,4},{"SB_ISTERR",0xA05F6908,4},
    {"SB_IML2NRM",0xA05F6910,4},{"SB_IML2EXT",0xA05F6914,4},{"SB_IML2ERR",0xA05F6918,4},
    {"SB_IML4NRM",0xA05F6920,4},{"SB_IML4EXT",0xA05F6924,4},{"SB_IML4ERR",0xA05F6928,4},
    {"SB_IML6NRM",0xA05F6930,4},{"SB_IML6EXT",0xA05F6934,4},{"SB_IML6ERR",0xA05F6938,4},
    {"SB_PDTNRM",0xA05F6940,4},{"SB_PDTEXT",0xA05F6944,4},{"SB_G2DTNRM",0xA05F6950,4},{"SB_G2DTEXT",0xA05F6954,4},
    {"SB_MDSTAR",0xA05F6C04,4},{"SB_MDTSEL",0xA05F6C10,4},{"SB_MDEN",0xA05F6C14,4},{"SB_MDST",0xA05F6C18,4},
    {"SB_MSYS",0xA05F6C80,4},{"SB_MST",0xA05F6C84,4},{"SB_MMSEL",0xA05F6CE8,4},{"SB_MTXDAD",0xA05F6CF4,4},
    {"SB_MRXDAD",0xA05F6CF8,4},{"SB_MRXDBD",0xA05F6CFC,4},
    {"SB_GDSTAR",0xA05F7404,4},{"SB_GDLEN",0xA05F7408,4},{"SB_GDDIR",0xA05F740C,4},{"SB_GDEN",0xA05F7414,4},
    {"SB_GDST",0xA05F7418,4},{"SB_G1SYSM",0xA05F74B0,4},{"SB_G1PROTST",0xA05F74EC,4},{"SB_GDSTARD",0xA05F74F4,4},
    {"SB_GDLEND",0xA05F74F8,4},
    {"SB_ADSTAG",0xA05F7800,4},{"SB_ADSTAR",0xA05F7804,4},{"SB_ADLEN",0xA05F7808,4},{"SB_ADDIR",0xA05F780C,4},
    {"SB_ADTSEL",0xA05F7810,4},{"SB_ADEN",0xA05F7814,4},{"SB_ADST",0xA05F7818,4},{"SB_ADSUSP",0xA05F781C,4},
    {"SB_E1TSEL",0xA05F7830,4},{"SB_E1EN",0xA05F7834,4},{"SB_E1ST",0xA05F7838,4},{"SB_E1SUSP",0xA05F783C,4},
    {"SB_E2SUSP",0xA05F785C,4},{"SB_DDSUSP",0xA05F787C,4},
    {"SB_G2ID",0xA05F7880,4},{"SB_G2DSTO",0xA05F7890,4},{"SB_G2TRTO",0xA05F7894,4},{"SB_G2MDMTO",0xA05F7898,4},
    {"SB_G2MDMW",0xA05F789C,4},{"SB_ADSTAGD",0xA05F78C0,4},{"SB_ADSTARD",0xA05F78C4,4},{"SB_ADLEND",0xA05F78C8,4},
    {"SB_PDSTAP",0xA05F7C00,4},{"SB_PDSTAR",0xA05F7C04,4},{"SB_PDLEN",0xA05F7C08,4},{"SB_PDDIR",0xA05F7C0C,4},
    {"SB_PDTSEL",0xA05F7C10,4},{"SB_PDEN",0xA05F7C14,4},{"SB_PDST",0xA05F7C18,4},
    {"SB_PDSTAPD",0xA05F7CF0,4},{"SB_PDSTARD",0xA05F7CF4,4},{"SB_PDLEND",0xA05F7CF8,4},
    /* ---- GD-ROM ATA block (no read side effects: alt status, not status) */
    {"GD_ALTSTAT",0xA05F7018,1},{"GD_ERROR",0xA05F7084,1},{"GD_IREASON",0xA05F7088,1},{"GD_SECTNUM",0xA05F708C,1},
    {"GD_BYCTLLO",0xA05F7090,1},{"GD_BYCTLHI",0xA05F7094,1},{"GD_DRVSEL",0xA05F7098,1},
    /* ---- PVR core */
    {"ID",0xA05F8000,4},{"REVISION",0xA05F8004,4},{"SOFTRESET",0xA05F8008,4},{"STARTRENDER",0xA05F8014,4},
    {"TEST_SELECT",0xA05F8018,4},{"PARAM_BASE",0xA05F8020,4},{"REGION_BASE",0xA05F802C,4},{"SPAN_SORT_CFG",0xA05F8030,4},
    {"VO_BORDER_COL",0xA05F8040,4},{"FB_R_CTRL",0xA05F8044,4},{"FB_W_CTRL",0xA05F8048,4},{"FB_W_LINESTRIDE",0xA05F804C,4},
    {"FB_R_SOF1",0xA05F8050,4},{"FB_R_SOF2",0xA05F8054,4},{"FB_R_SIZE",0xA05F805C,4},{"FB_W_SOF1",0xA05F8060,4},
    {"FB_W_SOF2",0xA05F8064,4},{"FB_X_CLIP",0xA05F8068,4},{"FB_Y_CLIP",0xA05F806C,4},{"FPU_SHAD_SCALE",0xA05F8074,4},
    {"FPU_CULL_VAL",0xA05F8078,4},{"FPU_PARAM_CFG",0xA05F807C,4},{"HALF_OFFSET",0xA05F8080,4},{"FPU_PERP_VAL",0xA05F8084,4},
    {"ISP_BACKGND_D",0xA05F8088,4},{"ISP_BACKGND_T",0xA05F808C,4},{"ISP_FEED_CFG",0xA05F8098,4},
    {"SDRAM_REFRESH",0xA05F80A0,4},{"SDRAM_ARB_CFG",0xA05F80A4,4},{"SDRAM_CFG",0xA05F80A8,4},
    {"FOG_COL_RAM",0xA05F80B0,4},{"FOG_COL_VERT",0xA05F80B4,4},{"FOG_DENSITY",0xA05F80B8,4},{"FOG_CLAMP_MAX",0xA05F80BC,4},
    {"FOG_CLAMP_MIN",0xA05F80C0,4},{"SPG_TRIGGER_POS",0xA05F80C4,4},{"SPG_HBLANK_INT",0xA05F80C8,4},
    {"SPG_VBLANK_INT",0xA05F80CC,4},{"SPG_CONTROL",0xA05F80D0,4},{"SPG_HBLANK",0xA05F80D4,4},{"SPG_LOAD",0xA05F80D8,4},
    {"SPG_VBLANK",0xA05F80DC,4},{"SPG_WIDTH",0xA05F80E0,4},{"TEXT_CONTROL",0xA05F80E4,4},{"VO_CONTROL",0xA05F80E8,4},
    {"VO_STARTX",0xA05F80EC,4},{"VO_STARTY",0xA05F80F0,4},{"SCALER_CTL",0xA05F80F4,4},{"PAL_RAM_CTRL",0xA05F8108,4},
    {"SPG_STATUS",0xA05F810C,4},{"FB_BURSTCTRL",0xA05F8110,4},{"FB_C_SOF",0xA05F8114,4},{"Y_COEFF",0xA05F8118,4},
    {"PT_ALPHA_REF",0xA05F811C,4},{"TA_OL_BASE",0xA05F8124,4},{"TA_ISP_BASE",0xA05F8128,4},{"TA_OL_LIMIT",0xA05F812C,4},
    {"TA_ISP_LIMIT",0xA05F8130,4},{"TA_NEXT_OPB",0xA05F8134,4},{"TA_ISP_CURRENT",0xA05F8138,4},
    {"TA_GLOB_TILE_CLIP",0xA05F813C,4},{"TA_ALLOC_CTRL",0xA05F8140,4},{"TA_LIST_INIT",0xA05F8144,4},
    {"TA_YUV_TEX_BASE",0xA05F8148,4},{"TA_YUV_TEX_CTRL",0xA05F814C,4},{"TA_YUV_TEX_CNT",0xA05F8150,4},
    {"TA_LIST_CONT",0xA05F8160,4},{"TA_NEXT_OPB_INIT",0xA05F8164,4},
    /* ---- AICA (G2): common registers, RTC */
    {"AICA_2800",0xA0702800,4},{"AICA_2804",0xA0702804,4},{"AICA_2808",0xA0702808,4},{"AICA_2880",0xA0702880,4},
    {"AICA_2884",0xA0702884,4},{"AICA_2890",0xA0702890,4},{"AICA_28B4",0xA07028B4,4},{"AICA_28B8",0xA07028B8,4},
    {"AICA_2C00",0xA0702C00,4},{"AICA_2D00",0xA0702D00,4},{"AICA_2D04",0xA0702D04,4},
    {"AICA_RTC_HI",0xA0710000,4},{"AICA_RTC_LO",0xA0710004,4},{"AICA_RTC_CTL",0xA0710008,4},
    /* ---- SH7091 on-chip (for the direct-boot state) */
    {"PTEH",0xFF000000,4},{"PTEL",0xFF000004,4},{"TTB",0xFF000008,4},{"TEA",0xFF00000C,4},{"MMUCR",0xFF000010,4},
    {"BASRA",0xFF000014,1},{"BASRB",0xFF000018,1},{"CCR",0xFF00001C,4},{"TRA",0xFF000020,4},{"EXPEVT",0xFF000024,4},
    {"INTEVT",0xFF000028,4},{"PTEA",0xFF000034,4},{"QACR0",0xFF000038,4},{"QACR1",0xFF00003C,4},
    {"PMCR1",0xFF000084,2},{"PMCR2",0xFF000088,2},
    {"BARA",0xFF200000,4},{"BAMRA",0xFF200004,1},{"BBRA",0xFF200008,2},{"BARB",0xFF20000C,4},{"BAMRB",0xFF200010,1},
    {"BBRB",0xFF200014,2},{"BDRB",0xFF200018,4},{"BDMRB",0xFF20001C,4},{"BRCR",0xFF200020,2},
    {"BCR1",0xFF800000,4},{"BCR2",0xFF800004,2},{"WCR1",0xFF800008,4},{"WCR2",0xFF80000C,4},{"WCR3",0xFF800010,4},
    {"MCR",0xFF800014,4},{"PCR",0xFF800018,2},{"RTCSR",0xFF80001C,2},{"RTCNT",0xFF800020,2},{"RTCOR",0xFF800024,2},
    {"RFCR",0xFF800028,2},{"PCTRA",0xFF80002C,4},{"PDTRA",0xFF800030,2},{"PCTRB",0xFF800040,4},{"PDTRB",0xFF800044,2},
    {"GPIOIC",0xFF800048,2},
    {"SAR0",0xFFA00000,4},{"DAR0",0xFFA00004,4},{"DMATCR0",0xFFA00008,4},{"CHCR0",0xFFA0000C,4},
    {"SAR1",0xFFA00010,4},{"DAR1",0xFFA00014,4},{"DMATCR1",0xFFA00018,4},{"CHCR1",0xFFA0001C,4},
    {"SAR2",0xFFA00020,4},{"DAR2",0xFFA00024,4},{"DMATCR2",0xFFA00028,4},{"CHCR2",0xFFA0002C,4},
    {"SAR3",0xFFA00030,4},{"DAR3",0xFFA00034,4},{"DMATCR3",0xFFA00038,4},{"CHCR3",0xFFA0003C,4},{"DMAOR",0xFFA00040,4},
    {"FRQCR",0xFFC00000,2},{"STBCR",0xFFC00004,1},{"WTCNT",0xFFC00008,1},{"WTCSR",0xFFC0000C,1},{"STBCR2",0xFFC00010,1},
    {"R64CNT",0xFFC80000,1},{"RSECCNT",0xFFC80004,1},{"RMINCNT",0xFFC80008,1},{"RHRCNT",0xFFC8000C,1},
    {"RWKCNT",0xFFC80010,1},{"RDAYCNT",0xFFC80014,1},{"RMONCNT",0xFFC80018,1},{"RYRCNT",0xFFC8001C,2},
    {"RCR1",0xFFC80038,1},{"RCR2",0xFFC8003C,1},
    {"ICR",0xFFD00000,2},{"IPRA",0xFFD00004,2},{"IPRB",0xFFD00008,2},{"IPRC",0xFFD0000C,2},
    {"TOCR",0xFFD80000,1},{"TSTR",0xFFD80004,1},{"TCOR0",0xFFD80008,4},{"TCNT0",0xFFD8000C,4},{"TCR0",0xFFD80010,2},
    {"TCOR1",0xFFD80014,4},{"TCNT1",0xFFD80018,4},{"TCR1",0xFFD8001C,2},{"TCOR2",0xFFD80020,4},{"TCNT2",0xFFD80024,4},
    {"TCR2",0xFFD80028,2},{"TCPR2",0xFFD8002C,4},
    {"SCSMR1",0xFFE00000,1},{"SCBRR1",0xFFE00004,1},{"SCSCR1",0xFFE00008,1},{"SCTDR1",0xFFE0000C,1},
    {"SCSSR1",0xFFE00010,1},{"SCSPTR1",0xFFE0001C,1},
    {"SCSMR2",0xFFE80000,2},{"SCBRR2",0xFFE80004,1},{"SCSCR2",0xFFE80008,2},{"SCFSR2",0xFFE80010,2},
    {"SCFCR2",0xFFE80018,2},{"SCFDR2",0xFFE8001C,2},{"SCSPTR2",0xFFE80020,2},{"SCLSR2",0xFFE80024,2},
};
#define NREG (sizeof(regs) / sizeof(regs[0]))

static uint32_t rd(const reg_t *r) {
    return r->w == 4 ? R32(r->addr) : r->w == 2 ? R16(r->addr) : R8(r->addr);
}

/* .data, not .bss: KOS clears the BSS after the early hook */
static uint32_t pre[NREG] __attribute__((section(".data"))) = {1};
static uint32_t pre_sr __attribute__((section(".data"))) = 1, pre_done __attribute__((section(".data"))) = 1;

static void early(void) {
    unsigned i;
    for(i = 0; i < NREG; i++) {
        if(regs[i].addr >= 0xA0700000 && regs[i].addr < 0xA0800000)
            while(R32(0xA05F688C) & 0x11) ;      /* G2 FIFO idle before an AICA read */
        pre[i] = rd(&regs[i]);
    }
    pre_done = 0xC0DE;
}
KOS_INIT_EARLY(early);

extern uint32_t _arch_old_sr, _arch_old_vbr, _arch_old_stack, _arch_old_fpscr;

/* ---------------------------------------------------------------------------------------------------------------- */
#define NEV 400
static uint32_t ev_t[NEV], ev_b[NEV], ev_s[NEV];

static void spg_events(uint32_t mask, uint32_t cycles, const char *tag) {
    uint32_t t0, n = 0, ist, st;
    int old = irq_disable();
    R32(0xA05F6900) = 0x38;                    /* ack vblank-in / vblank-out / hblank */
    t0 = CYC;
    while(n < NEV && (CYC - t0) < cycles) {
        ist = R32(0xA05F6900) & mask;
        if(ist) {
            st = R32(0xA05F810C);
            ev_t[n] = CYC - t0; ev_b[n] = ist; ev_s[n] = st; n++;
            R32(0xA05F6900) = ist;
        }
    }
    R32(0xA05F6900) = 0x38;
    irq_restore(old);
    printf("B %s events %lu\n", tag, (unsigned long)n);
    for(uint32_t i = 0; i < n && i < 60; i++)
        printf("B %s %lu %lu %02lx %08lx\n", tag, (unsigned long)i, (unsigned long)ev_t[i], (unsigned long)ev_b[i],
               (unsigned long)ev_s[i]);
}

static void spg_lines(void) {
    /* scanline changes of SPG_STATUS: 40 transitions from a random point, then 40 around the vblank-in line */
    uint32_t t0, last, s, n = 0, tt[80], ss[80];
    int old = irq_disable();
    t0 = CYC; last = R32(0xA05F810C) & 0x3FF;
    while(n < 40 && (CYC - t0) < 4000000u) {
        s = R32(0xA05F810C);
        if((s & 0x3FF) != last) { tt[n] = CYC - t0; ss[n] = s; n++; last = s & 0x3FF; }
    }
    /* wait for line 515, then log to 535 */
    t0 = CYC;
    while(((R32(0xA05F810C) & 0x3FF) != 515) && (CYC - t0) < 8000000u) ;
    last = 515;
    while(n < 80 && (CYC - t0) < 12000000u) {
        s = R32(0xA05F810C);
        if((s & 0x3FF) != last) { tt[n] = CYC - t0; ss[n] = s; n++; last = s & 0x3FF; }
    }
    irq_restore(old);
    for(uint32_t i = 0; i < n; i++)
        printf("B line %lu %lu %08lx\n", (unsigned long)i, (unsigned long)tt[i], (unsigned long)ss[i]);
}

/* ---------------------------------------------------------------------------------------------------------------- */
static uint8_t cbuf[8192] __attribute__((aligned(32)));
static uint32_t sqsrc[8] __attribute__((aligned(32)));

#define TIME8(label, stmt) do {                                                    \
        uint32_t _t0, _t1; int _k;                                                 \
        int _old = irq_disable();                                                  \
        _t0 = CYC;                                                                 \
        for(_k = 0; _k < 32; _k++) { stmt; stmt; stmt; stmt; stmt; stmt; stmt; stmt; } \
        _t1 = CYC;                                                                 \
        irq_restore(_old);                                                         \
        printf("B lat %-24s %lu cycles / 256 = %lu.%02lu\n", label, (unsigned long)(_t1 - _t0), \
               (unsigned long)((_t1 - _t0) / 256), (unsigned long)(((_t1 - _t0) % 256) * 100 / 256)); \
    } while(0)

static volatile uint32_t sink;

static void latencies(void) {
    volatile uint32_t *ramc = (volatile uint32_t *)cbuf;
    volatile uint32_t *ramu = (volatile uint32_t *)(((uint32_t)cbuf & 0x1FFFFFFF) | 0xA0000000);
    uint32_t v = 0;
    TIME8("cached RAM read (hit)", v += ramc[0]);
    TIME8("P2 RAM read", v += ramu[0]);
    TIME8("P2 RAM write", ramu[1] = v);
    TIME8("P2 VRAM32 read", v += R32(0xA5700000));
    TIME8("P2 VRAM32 write", R32(0xA5700004) = v);
    TIME8("P2 VRAM64 read", v += R32(0xA4700000));
    TIME8("PVR reg read (ID)", v += R32(0xA05F8000));
    TIME8("SB reg read (ISTNRM)", v += R32(0xA05F6900));
    TIME8("SB reg read (FFST)", v += R32(0xA05F688C));
    TIME8("GD altstat read8", v += R8(0xA05F7018));
    TIME8("BIOS ROM read32", v += R32(0xA0000000));
    TIME8("BIOS ROM read16", v += R16(0xA0000000));
    TIME8("flash read8", v += R8(0xA0200000));
    TIME8("flash read32", v += R32(0xA0200000));
    g2_fifo_wait();
    TIME8("AICA reg read32 (2C00)", v += R32(0xA0702C00));
    TIME8("AICA RTC read32", v += R32(0xA0710004));
    TIME8("AICA RAM read32", v += R32(0xA0810000));
    sink = v;

    /* AICA RAM PIO write: 8 longwords, then time until SB_FFST drains */
    {
        uint32_t t0, t8, n = 0, last = 0xFFFFFFFF, vals[32], ts[32], f;
        int old = irq_disable();
        g2_fifo_wait();
        t0 = CYC;
        for(int i = 0; i < 8; i++) R32(0xA0810000 + 4 * i) = 0x11110000 + i;
        t8 = CYC - t0;
        while(n < 32 && (CYC - t0) < 200000) {
            f = R32(0xA05F688C);
            if(f != last) { vals[n] = f; ts[n] = CYC - t0; n++; last = f; if(f == 0) break; }
        }
        irq_restore(old);
        printf("B ffst after 8 AICA writes (%lu cycles to issue):", (unsigned long)t8);
        for(uint32_t i = 0; i < n; i++) printf(" %lu:%lx", (unsigned long)ts[i], (unsigned long)vals[i]);
        printf("\n");
        printf("B aica ram readback %08lx %08lx\n", (unsigned long)R32(0xA0810000), (unsigned long)R32(0xA081001C));
    }

    /* cache-line miss (P1 read after OCBP) and hit */
    {
        uint32_t t0, t1, t2, i;
        int old = irq_disable();
        for(i = 0; i < sizeof cbuf; i += 32) __asm__ volatile("ocbp @%0" : : "r"(cbuf + i) : "memory");
        t0 = CYC;
        for(i = 0; i < sizeof cbuf; i += 32) v += *(volatile uint32_t *)(cbuf + i);
        t1 = CYC;
        for(i = 0; i < sizeof cbuf; i += 32) v += *(volatile uint32_t *)(cbuf + i);
        t2 = CYC;
        irq_restore(old);
        printf("B lat P1 line miss (256 lines) %lu cycles, re-read hits %lu cycles\n", (unsigned long)(t1 - t0),
               (unsigned long)(t2 - t1));
        /* dirty-line write-back cost: dirty the buffer, then OCBWB each line */
        for(i = 0; i < sizeof cbuf; i += 4) *(volatile uint32_t *)(cbuf + i) = i;
        old = irq_disable();
        t0 = CYC;
        for(i = 0; i < sizeof cbuf; i += 32) __asm__ volatile("ocbwb @%0" : : "r"(cbuf + i) : "memory");
        t1 = CYC;
        irq_restore(old);
        printf("B lat OCBWB dirty line (256 lines) %lu cycles\n", (unsigned long)(t1 - t0));
    }

    /* store-queue bursts: 64 bursts back to back to RAM (our buffer), VRAM (32-bit path, offscreen), AICA RAM */
    {
        const char *tn[3] = {"SQ->RAM", "SQ->VRAM32", "SQ->AICA RAM"};
        uint32_t ext[3] = {((uint32_t)cbuf & 0x1FFFFFE0), 0x05700000, 0x00810000};
        uint32_t q0 = R32(0xFF000038), q1 = R32(0xFF00003C);
        for(int k = 0; k < 8; k++) sqsrc[k] = 0x5A5A0000 + k;
        for(unsigned t = 0; t < 3; t++) {
            uint32_t t0, t1, t2, t3;
            if(t == 2) g2_fifo_wait();
            int old = irq_disable();
            R32(0xFF000038) = (ext[t] >> 24) & 0x1C; R32(0xFF00003C) = (ext[t] >> 24) & 0x1C;
            volatile uint32_t *sq = (volatile uint32_t *)(0xE0000000 | (ext[t] & 0x03FFFFE0));
            t0 = CYC;
            for(int b = 0; b < 64; b++) {
                volatile uint32_t *d = sq + b * 8;          /* consecutive 32-byte blocks, SQ0 / SQ1 alternate */
                d[0] = sqsrc[0]; d[1] = sqsrc[1]; d[2] = sqsrc[2]; d[3] = sqsrc[3];
                d[4] = sqsrc[4]; d[5] = sqsrc[5]; d[6] = sqsrc[6]; d[7] = sqsrc[7];
                __asm__ volatile("pref @%0" : : "r"(d) : "memory");
            }
            t1 = CYC;
            sq[0] = 0; sq[8] = 0;                          /* sq_wait: stalls until both queues are free */
            t2 = CYC;
            R32(0xFF000038) = q0; R32(0xFF00003C) = q1;
            irq_restore(old);
            if(t == 2) g2_fifo_wait();
            t3 = CYC;
            printf("B lat %-14s 64 bursts issue %lu cycles (+drain %lu, +fifo %lu)\n", tn[t], (unsigned long)(t1 - t0),
                   (unsigned long)(t2 - t1), (unsigned long)(t3 - t2));
        }
        printf("B sq readback RAM %08lx VRAM %08lx AICA %08lx\n",
               (unsigned long)R32(((uint32_t)cbuf & 0x1FFFFFFF) | 0xA0000000 | 0x1C), (unsigned long)R32(0xA570001C),
               (unsigned long)R32(0xA081001C));
    }
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void mirrors(void) {
    volatile uint32_t *p = (volatile uint32_t *)(((uint32_t)cbuf & 0x00FFFFFF) | 0xAC000000);
    p[4] = 0xB0A7D0C5;
    printf("B mirror RAM AC %08lx AD %08lx AE %08lx AF %08lx\n", (unsigned long)p[4],
           (unsigned long)*(volatile uint32_t *)((uint32_t)(p + 4) + 0x01000000),
           (unsigned long)*(volatile uint32_t *)((uint32_t)(p + 4) + 0x02000000),
           (unsigned long)*(volatile uint32_t *)((uint32_t)(p + 4) + 0x03000000));
    R32(0xA5700010) = 0x7EA7BEEF;
    printf("B mirror VRAM32 A5700010 %08lx A7700010 %08lx A5F00010 %08lx\n", (unsigned long)R32(0xA5700010),
           (unsigned long)R32(0xA7700010), (unsigned long)R32(0xA5F00010));
    printf("B mirror VRAM64 A4700010 %08lx A6700010 %08lx\n", (unsigned long)R32(0xA4700010), (unsigned long)R32(0xA6700010));
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("B start pre_done %lx old_sr %08lx old_vbr %08lx old_stack %08lx old_fpscr %08lx\n", (unsigned long)pre_done,
           (unsigned long)_arch_old_sr, (unsigned long)_arch_old_vbr, (unsigned long)_arch_old_stack,
           (unsigned long)_arch_old_fpscr);
    for(unsigned i = 0; i < NREG; i++) {
        if(regs[i].addr >= 0xA0700000 && regs[i].addr < 0xA0800000) g2_fifo_wait();
        uint32_t post = rd(&regs[i]);
        printf("B reg %-18s %08lx pre %08lx post %08lx\n", regs[i].name, (unsigned long)regs[i].addr,
               (unsigned long)pre[i], (unsigned long)post);
    }
    int st = -9, dt = -9;
    int rv = cdrom_get_status(&st, &dt);
    printf("B cdrom_get_status rv %d status %d disc_type %d\n", rv, st, dt);
    spg_events(0x18, 14000000u, "vbl");      /* vblank-in / vblank-out, 70 ms */
    spg_events(0x20, 2000000u, "hbl");       /* hblank-in, 10 ms */
    spg_lines();
    latencies();
    mirrors();
    printf("B done\n");
    return 0;
}
