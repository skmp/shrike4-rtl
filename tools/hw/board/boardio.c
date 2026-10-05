/* boardio.c -- Dreamcast board timing facts for rtl/v1/spec/board.md (second console run).
 *
 * Built WITHOUT KOS maple and CD-ROM (KOS_INIT_FLAGS(INIT_IRQ | INIT_FS_PTY); note INIT_CDROM == INIT_SIP pulls
 * in maple, and without an INIT_FS_* flag printf has no stdout), so this program owns the maple DMA engine:
 *  1. KOS init duration (timer_ms_gettime64 at main entry).
 *  2. Maple DMA: DEVINFO to ports A-D in one list, then single frames to an empty port and to port A, then a
 *     GETCOND to the controller.  Per run: cycles until SB_MDST clears and until ISTNRM bit 12, the receive words,
 *     ISTERR, and the SH4 DMAC channel-0 registers afterwards (DDT evidence).
 *  3. GD-ROM raw ATA/SPI timing (the drive holds dcload's CD-R): REQ_STAT, TEST_UNIT_READY, REQ_ERROR: cycles from
 *     the PACKET command to DRQ, from the last packet word to INTRQ (ISTEXT bit 0), status / ireason / byte count,
 *     ISTNRM summary bit 30 while INTRQ is pending.
 *  4. ISTNRM hblank-in (bit 5) events for 40 ms.
 *  5. The tools/boot/hello level-1 check: vblank IRQs counted by a KOS vblank handler during thd_sleep(100).
 * IRQs are masked around every bounded loop; SB_MDSTAR / MDEN are restored; every GD command is run to completion
 * (INTRQ cleared by the status read); on a GD timeout an ATA soft reset (0x08) puts the drive back in the idle state.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <arch/timer.h>

KOS_INIT_FLAGS(INIT_IRQ | INIT_FS_PTY);

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))
#define CYC    R32(0xFF100008)

#define ISTNRM 0xA05F6900
#define ISTEXT 0xA05F6904
#define ISTERR 0xA05F6908
#define MDSTAR 0xA05F6C04
#define MDEN   0xA05F6C14
#define MDST   0xA05F6C18

static uint32_t mbuf[2048] __attribute__((aligned(32)));

static void maple_run(const char *tag, const uint32_t *list, int nwords, int nrecv) {
    volatile uint32_t *cmd = (volatile uint32_t *)(((uint32_t)mbuf & 0x1FFFFFFF) | 0xA0000000);
    volatile uint32_t *rcv = cmd + 512;                   /* 4 receive buffers of 256 words */
    uint32_t phys_rcv = ((uint32_t)mbuf & 0x1FFFFFFF) + 2048;
    uint32_t t0, tst = 0, tint = 0, now, ist, err;
    int i, k = 0;
    for(i = 0; i < 1024; i++) rcv[i] = 0xDEADBEEF;
    for(i = 0; i < nwords; i++) {
        uint32_t w = list[i];
        if(w == 0xFFFF0000) w = phys_rcv;                  /* receive-address placeholders, 1 KB apart */
        if(w == 0xFFFF0001) w = phys_rcv + 1024;
        if(w == 0xFFFF0002) w = phys_rcv + 2048;
        if(w == 0xFFFF0003) w = phys_rcv + 3072;
        cmd[i] = w;
    }
    int old = irq_disable();
    R32(ISTNRM) = 1 << 12; R32(ISTERR) = 0xF00;
    R32(MDSTAR) = (uint32_t)mbuf & 0x1FFFFFFF;
    t0 = CYC;
    R32(MDST) = 1;
    while(1) {
        now = CYC - t0;
        if(!tst && !(R32(MDST) & 1)) tst = now;
        if(!tint && (R32(ISTNRM) & (1 << 12))) tint = now;
        if((tst && tint) || now > 20000000u) break;          /* 100 ms */
    }
    ist = R32(ISTNRM); err = R32(ISTERR);
    R32(ISTNRM) = 1 << 12; R32(ISTERR) = 0xF00;
    irq_restore(old);
    printf("B maple %s mdst0 %lu int %lu istnrm %08lx isterr %08lx SAR0 %08lx DAR0 %08lx CHCR0 %08lx DMATCR0 %08lx"
           " MTXDAD %08lx MRXDAD %08lx\n", tag, (unsigned long)tst, (unsigned long)tint, (unsigned long)ist,
           (unsigned long)err, (unsigned long)R32(0xFFA00000), (unsigned long)R32(0xFFA00004),
           (unsigned long)R32(0xFFA0000C), (unsigned long)R32(0xFFA00008), (unsigned long)R32(0xA05F6CF4),
           (unsigned long)R32(0xA05F6CF8));
    for(k = 0; k < nrecv; k++) {
        printf("B maple %s rx%d", tag, k);
        for(i = 0; i < 8; i++) printf(" %08lx", (unsigned long)rcv[k * 256 + i]);
        printf("\n");
    }
}

static void maple_tests(void) {
    uint32_t save_star = R32(MDSTAR), save_en = R32(MDEN);
    printf("B maple start MDEN %lx MDSTAR %08lx MSYS %08lx MDTSEL %lx MST %08lx\n", (unsigned long)save_en,
           (unsigned long)save_star, (unsigned long)R32(0xA05F6C80), (unsigned long)R32(0xA05F6C10),
           (unsigned long)R32(0xA05F6C84));
    R32(MDEN) = 1;
    /* DEVINFO (cmd 1) to ports A..D unit 0: word0 = len | port<<16 | last<<31, word1 = rx addr,
       word2 = cmd | dst<<8 | src<<16 | len<<24 */
    static const uint32_t all4[] = {
        0x00000000, 0xFFFF0000, 0x00002001,
        0x00010000, 0xFFFF0001, 0x00406001,
        0x00020000, 0xFFFF0002, 0x0080A001,
        0x80030000, 0xFFFF0003, 0x00C0E001};
    maple_run("devinfo_ABCD", all4, 12, 4);
    static const uint32_t portD[] = {0x80030000, 0xFFFF0000, 0x00C0E001};
    maple_run("devinfo_D", portD, 3, 1);
    static const uint32_t portA[] = {0x80000000, 0xFFFF0000, 0x00002001};
    maple_run("devinfo_A", portA, 3, 1);
    static const uint32_t getcond[] = {0x80000001, 0xFFFF0000, 0x01002009, 0x01000000};
    maple_run("getcond_A", getcond, 4, 1);
    static const uint32_t twoA[] = {0x00000000, 0xFFFF0000, 0x00002001, 0x80000001, 0xFFFF0001, 0x01002009, 0x01000000};
    maple_run("devinfo+getcond_A", twoA, 7, 2);
    R32(MDSTAR) = save_star; R32(MDEN) = save_en;
}

/* ---------------------------------------------------------------------------------------------------------------- */
#define GD_ALT   0xA05F7018
#define GD_DATA  0xA05F7080
#define GD_FEAT  0xA05F7084
#define GD_IRSN  0xA05F7088
#define GD_SNUM  0xA05F708C
#define GD_BCLO  0xA05F7090
#define GD_BCHI  0xA05F7094
#define GD_STAT  0xA05F709C

static int gd_wait(uint32_t mask, uint32_t val, uint32_t t0, uint32_t lim, uint32_t *t) {
    while(((R8(GD_ALT) & mask) != val)) if(CYC - t0 > lim) return -1;
    *t = CYC - t0;
    return 0;
}

static void gd_cmd(const char *tag, uint8_t op, uint8_t alloc) {
    uint8_t pkt[12] = {op, 0, 0, 0, alloc, 0, 0, 0, 0, 0, 0, 0};
    uint32_t t0, tdrq = 0, tpkt = 0, tint = 0, tbsy = 0, tint2 = 0, ist_pend = 0, iext = 0;
    uint16_t data[8] = {0};
    uint8_t st1 = 0, ir1 = 0, bc_lo = 0, bc_hi = 0, st2 = 0, ir2 = 0, alt_after = 0, err = 0;
    int ok = 1, nd = 0;
    int old = irq_disable();
    t0 = CYC;
    if(gd_wait(0x88, 0x00, t0, 20000000u, &tdrq) < 0) { ok = 0; goto out; }       /* idle: BSY = DRQ = 0 */
    (void)R8(GD_STAT);                                                               /* drop a stale INTRQ */
    R8(GD_FEAT) = 0; R8(GD_BCLO) = alloc; R8(GD_BCHI) = 0;
    t0 = CYC;
    R8(GD_STAT) = 0xA0;                                                              /* PACKET */
    if(gd_wait(0x88, 0x08, t0, 20000000u, &tdrq) < 0) { ok = 0; goto out; }
    for(int i = 0; i < 6; i++) R16(GD_DATA) = pkt[2 * i] | (pkt[2 * i + 1] << 8);
    tpkt = CYC - t0;
    /* BSY seen after the packet? then INTRQ */
    while(!(R32(ISTEXT) & 1)) {
        if(!tbsy && (R8(GD_ALT) & 0x80)) tbsy = CYC - t0;
        if(CYC - t0 > 40000000u) { ok = 0; goto out; }
    }
    tint = CYC - t0;
    ist_pend = R32(ISTNRM); iext = R32(ISTEXT);
    st1 = R8(GD_STAT); ir1 = R8(GD_IRSN); bc_lo = R8(GD_BCLO); bc_hi = R8(GD_BCHI);
    alt_after = R8(GD_ALT);
    if(st1 & 0x08) {                                                                 /* data phase */
        int n = (bc_lo | (bc_hi << 8)) / 2;
        for(int i = 0; i < n; i++) { uint16_t w = R16(GD_DATA); if(i < 8) data[i] = w; nd++; }
        while(!(R32(ISTEXT) & 1)) if(CYC - t0 > 40000000u) { ok = 0; goto out; }
        tint2 = CYC - t0;
        st2 = R8(GD_STAT); ir2 = R8(GD_IRSN);
    }
    err = R8(GD_FEAT);
out:
    if(!ok) { R8(GD_STAT) = 0x08; (void)R8(GD_STAT); }                             /* ATA soft reset */
    irq_restore(old);
    printf("B gd %s ok %d drq %lu pkt %lu bsy %lu int %lu int2 %lu | st %02x ir %02x bc %u alt %02x istnrm %08lx"
           " istext %08lx | st2 %02x ir2 %02x err %02x secnum %02x | data(%d)", tag, ok, (unsigned long)tdrq,
           (unsigned long)tpkt, (unsigned long)tbsy, (unsigned long)tint, (unsigned long)tint2, st1, ir1,
           bc_lo | (bc_hi << 8), alt_after, (unsigned long)ist_pend, (unsigned long)iext, st2, ir2, err,
           R8(GD_SNUM), nd);
    for(int i = 0; i < 5 && i < nd; i++) printf(" %04x", data[i]);
    printf("\n");
}

static void gd_tests(void) {
    printf("B gd start alt %02x secnum %02x istext %08lx gdst %08lx\n", R8(GD_ALT), R8(GD_SNUM),
           (unsigned long)R32(ISTEXT), (unsigned long)R32(0xA05F7418));
    gd_cmd("req_stat", 0x10, 10);
    gd_cmd("test_unit", 0x00, 0);
    gd_cmd("req_error", 0x13, 10);
    gd_cmd("req_stat2", 0x10, 10);
}

static void hblank(void) {
    uint32_t t0, n = 0, tt[64], ss[64], ist;
    int old = irq_disable();
    R32(ISTNRM) = 0x20;
    t0 = CYC;
    while(n < 64 && CYC - t0 < 8000000u) {
        ist = R32(ISTNRM) & 0x20;
        if(ist) { ss[n] = R32(0xA05F810C); tt[n] = CYC - t0; n++; R32(ISTNRM) = 0x20; }
    }
    irq_restore(old);
    printf("B hbl events %lu (SPG_HBLANK_INT %08lx)\n", (unsigned long)n, (unsigned long)R32(0xA05F80C8));
    for(uint32_t i = 0; i < n && i < 16; i++) printf("B hbl %lu %lu %08lx\n", (unsigned long)i, (unsigned long)tt[i],
                                                    (unsigned long)ss[i]);
}

static volatile uint32_t vbl_count;
static void vbl(uint32_t code, void *data) { (void)code; (void)data; vbl_count++; }

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t ms = timer_ms_gettime64();
    printf("B init_ms %lu\n", (unsigned long)ms);
    int h = vblank_handler_add(vbl, NULL);
    uint32_t v0 = vbl_count, c0 = CYC;
    thd_sleep(100);
    uint32_t v1 = vbl_count, c1 = CYC;
    vblank_handler_remove(h);
    printf("B vblank irqs in thd_sleep(100): %lu, %lu cycles, now %lu ms\n", (unsigned long)(v1 - v0),
           (unsigned long)(c1 - c0), (unsigned long)timer_ms_gettime64());
    maple_tests();
    gd_tests();
    hblank();
    printf("B done\n");
    return 0;
}
