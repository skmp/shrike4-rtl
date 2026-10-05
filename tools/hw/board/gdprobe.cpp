// gdprobe.cpp -- run syscalls.bin's own GD-ROM / sysinfo / flashrom syscalls on the reference ISS against a stub
// Dreamcast board, logging every access that leaves main RAM.  Question: what does the syscall driver do to the
// board when KOS calls syscall_gdrom_init / CMD_INIT / check_drive / sector_mode, and what must a no-disc drive answer.
// Host tool (not a console program).  Build and run:
//   g++ -O1 -std=c++17 -I../../../tb -o gdprobe gdprobe.cpp && ./gdprobe [--nodrive] [--blankflash]
// Results are recorded in rtl/v1/spec/board.md ("GD-ROM: what syscalls.bin does").
#include "sh4_iss.h"
#include <vector>
#include <map>
#include <cstdlib>

static bool g_log = true;
static int g_drive = 1;          // 0 = no drive (bus floats), 1 = drive, no disc
static uint64_t g_gdreads = 0;

struct Gd {
    // ATA/SPI register file (G1 bus, 0x005F7000..)
    uint8_t status = 0x50, error = 0, ireason = 0, secnum = 0x07, seccnt = 0, features = 0, drvsel = 0;
    uint16_t bytecount = 0;
    int state = 0;              // 0 waitcmd, 1 waitpacket, 2 pio out
    uint16_t pkt[6]; int pktn = 0;
    std::vector<uint16_t> pio; size_t pioi = 0;
    uint8_t skey = 6, asc = 0x29, ascq = 0;
    bool intrq = false;
    void done(bool check) {                  // procpacketdone
        status = 0x40 | (check ? 1 : 0); ireason = 3; intrq = true; state = 0;
    }
    void pio_out(const uint8_t *b, int len) {
        pio.clear(); for (int i = 0; i + 1 < len; i += 2) pio.push_back(b[i] | (b[i + 1] << 8)); pioi = 0;
        if (pio.empty()) { done(false); return; }
        bytecount = len; ireason = 2; status = 0x48; intrq = true; state = 2;   // IO=1 CoD=0, DRDY|DRQ
    }
    void spi() {
        uint8_t *c = (uint8_t *)pkt;
        if (g_log) printf("  GD SPI %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", c[0], c[1], c[2], c[3],
                          c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11]);
        switch (c[0]) {
        case 0x00: { bool ck = skey != 0; if (!ck) { skey = 2; asc = 0x3A; ck = true; } done(ck); break; } // TEST UNIT
        case 0x10: { uint8_t s[10] = {secnum, 0, 4, 2, 0, 0, 0, 0, 0, 0}; pio_out(s + c[2], c[4]); break; } // REQ_STAT
        case 0x11: { static const uint16_t r11[] = {0x0000,0x0000,0xb400,0x0019,0x0800,0x4553,0x2020,0x2020,0x2020,
                     0x6552,0x2076,0x2e36,0x3334,0x3939,0x3430,0x3830}; pio_out((const uint8_t *)r11 + c[2], c[4]); break; }
        case 0x13: { uint8_t r[10] = {0xF0, 0, skey, 0, 0, 0, 0, 0, asc, ascq}; pio_out(r, c[4]); skey = 2; asc = 0x3A; ascq = 0; break; }
        default:   skey = 2; asc = 0x3A; ascq = 0; done(true); break;      // everything else: NOT READY, no medium
        }
    }
    uint32_t rd(uint32_t a, int sz) {
        g_gdreads++;
        if (!g_drive) return sz == 1 ? 0xFF : sz == 2 ? 0xFFFF : 0xFFFFFFFF;
        switch (a & 0xFF) {
        case 0x18: return status;
        case 0x80: { uint16_t v = 0; if (state == 2 && pioi < pio.size()) { v = pio[pioi++]; if (pioi == pio.size()) done(false); } return v; }
        case 0x84: return error | (skey << 4);
        case 0x88: return ireason;
        case 0x8C: return secnum;
        case 0x90: return bytecount & 0xFF;
        case 0x94: return bytecount >> 8;
        case 0x98: return drvsel;
        case 0x9C: intrq = false; return status;
        }
        return 0;
    }
    void wr(uint32_t a, uint32_t v, int sz) {
        if (!g_drive) return;
        switch (a & 0xFF) {
        case 0x80: if (state == 1) { pkt[pktn++] = v; if (pktn == 6) { status = 0x80; spi(); } } break;
        case 0x84: features = v; break;
        case 0x88: seccnt = v; break;
        case 0x90: bytecount = (bytecount & 0xFF00) | (v & 0xFF); break;
        case 0x94: bytecount = (bytecount & 0xFF) | (v << 8); break;
        case 0x98: drvsel = v; break;
        case 0x9C:
            if (g_log) printf("  GD ATA cmd %02X\n", v);
            if (v == 0xA0) { state = 1; pktn = 0; status = 0x58 & ~0x40 | 0x08; ireason = 1; }        // DRQ, CoD
            else if (v == 0x08) { status = 0x50; secnum = 0x07; error = 0; state = 0; }            // soft reset
            else if (v == 0xEF) { status = 0x50; intrq = true; }
            else { error = 4; status = 0x51; intrq = true; }
            break;
        }
    }
} gd;

struct Board : Sh4Mem {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16 << 20), flash = std::vector<uint8_t>(128 << 10, 0xFF);
    std::map<uint32_t, uint32_t> sb;               // 0x005F68xx..0x005F7Cxx system-bus registers (plain RW)
    uint32_t tmu_tcnt = 0xFFFFFFFF;
    uint64_t flash_reads = 0, bios_reads = 0;
    uint32_t rdraw(const std::vector<uint8_t> &m, uint32_t o, int sz) { uint32_t v = 0; memcpy(&v, &m[o], sz); return v; }
    void log(const char *k, uint32_t a, int sz, uint32_t v) { if (g_log) printf("  %s %08X.%d %s %08X\n", k, a, sz, "=", v); }
    uint32_t rd(uint32_t a, int sz) override {
        if ((a >> 26) == 3) return rdraw(ram, a & 0xFFFFFF, sz);
        uint32_t v = 0;
        if (a < 0x00200000) { bios_reads++; return 0; }
        if (a >= 0x00200000 && a < 0x00220000) { flash_reads++; v = rdraw(flash, a & 0x1FFFF, sz); log("FLASH rd", a, sz, v); return v; }
        if (a >= 0x005F7000 && a < 0x005F7100) { v = gd.rd(a, sz); if (g_log) printf("  GD rd %08X.%d = %X\n", a, sz, v); return v; }
        if (a >= 0x005F6800 && a < 0x005F8000) {
            if (a == 0x005F6904) v = gd.intrq ? 1 : 0;              // ISTEXT bit0 = GD INTRQ
            else if (a == 0x005F6900) v = (gd.intrq ? 0x40000000 : 0) | sb[a];
            else if (a == 0x005F74EC) v = 3;
            else v = sb[a];
            log("SB rd", a, sz, v); return v;
        }
        if (a >= 0x1F000000) {                                       // P4 / area 7
            if (a == 0x1FD8002C) v = 0; else if (a == 0x1FD80024) v = tmu_tcnt -= 1000;
            log("P4 rd", a, sz, v); return v;
        }
        log("?? rd", a, sz, 0); return 0;
    }
    void wr(uint32_t a, int sz, uint32_t v) override {
        if ((a >> 26) == 3) { memcpy(&ram[a & 0xFFFFFF], &v, sz); return; }
        if (a >= 0x00200000 && a < 0x00220000) { log("FLASH wr", a, sz, v); return; }
        if (a >= 0x005F7000 && a < 0x005F7100) { if (g_log) printf("  GD wr %08X.%d = %X\n", a, sz, v); gd.wr(a, v, sz); return; }
        if (a >= 0x005F6800 && a < 0x005F8000) { log("SB wr", a, sz, v); sb[a] = v; return; }
        log(a >= 0x1F000000 ? "P4 wr" : "?? wr", a, sz, v);
    }
};

static Board B;
static Sh4Iss iss(&B);
static const uint32_t HALT = 0x8C00F000, STK = 0x8CF00000;

static uint32_t rd32(uint32_t a) { return B.rd(a & 0x1FFFFFFF, 4); }
static uint32_t call(const char *what, uint32_t vec, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7) {
    if (g_log) printf("CALL %s (vec %08X -> %08X) r4=%X r5=%X r6=%X r7=%X\n", what, vec, rd32(vec), r4, r5, r6, r7);
    iss.halted = iss.error = false; iss.slot_pending = false;
    iss.s.pc = rd32(vec); iss.s.pr = HALT; iss.s.r[15] = STK; iss.s.r[4] = r4; iss.s.r[5] = r5; iss.s.r[6] = r6; iss.s.r[7] = r7;
    uint64_t n = iss.run(50000000);
    if (iss.error) { printf("  ISS ERROR %s at pc %08X op %04X\n", iss.errmsg, iss.s.pc, B.rd(iss.s.pc & 0x1FFFFFFF, 2)); exit(1); }
    if (g_log) printf("  -> r0=%08X (%d) after %llu instructions\n", iss.s.r[0], (int)iss.s.r[0], (unsigned long long)n);
    return iss.s.r[0];
}

int main(int argc, char **argv) {
    const char *sys = "../../../dreamcast-data/syscalls.bin";       // run from this directory (the header)
    const char *fl = "../../../dreamcast-data/dc_flash.bin";         // a console's flash image (local)
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nodrive")) g_drive = 0;
        if (!strcmp(argv[i], "--blankflash")) fl = nullptr;
    }
    FILE *f = fopen(sys, "rb"); fread(&B.ram[0], 1, 32768, f); fclose(f);
    if (fl) { f = fopen(fl, "rb"); fread(&B.flash[0], 1, 128 << 10, f); fclose(f); }
    B.wr(HALT & 0x1FFFFFFF, 2, 0x001B);                  // SLEEP
    iss.s.sr = 0x400000F0; iss.s.vbr = 0x8C000000; iss.s.fpscr = 0x00040001; iss.s.gbr = 0x8C000000;

    call("sysinfo_init", 0x8C0000B0, 0, 0, 0, 0);
    printf("  sysinfo id @8C000068: "); for (int i = 0; i < 8; i++) printf("%02X", B.ram[0x68 + i]); printf("  region @8C000070: ");
    for (int i = 0; i < 5; i++) printf("%c", B.ram[0x70 + i]); printf("\n");
    uint32_t buf = 0x8C0E0000;
    call("flashrom_info(2)", 0x8C0000B8, 2, buf, 0, 0);
    printf("  info: %08X %08X\n", rd32(buf), rd32(buf + 4));
    call("romfont_address", 0x8C0000B4, 0, 0, 0, 0);   // r1 selects -- set below
    iss.s.r[1] = 0; call("romfont_address(r1=0)", 0x8C0000B4, 0, 0, 0, 0);

    call("gdrom_init", 0x8C0000BC, 0, 0, 0, 3);
    if (g_drive == 0) { uint32_t st0 = 0x8C0E2000; int rr = (int)call("check_drive(nodrive)", 0x8C0000BC, st0, 0, 0, 4);
        printf("check_drive with no drive -> %d (%d %d)\n", rr, (int)rd32(st0), (int)rd32(st0 + 4)); return 0; }
    // KOS cdrom_reinit: CMD_INIT (24) with exec_server + check_command polling
    uint32_t st = 0x8C0E1000; int r = 0;
    for (int pass = 0; pass < 3; pass++) {
    uint32_t h = 0;
    for (int k = 0; k < 10 && (int)h <= 0; k++) { h = call("send_command(CMD_INIT)", 0x8C0000BC, 24, 0, 0, 0); call("exec_server", 0x8C0000BC, 0, 0, 0, 2); }
    for (int k = 0; k < 200; k++) {
        call("exec_server", 0x8C0000BC, 0, 0, 0, 2);
        r = (int)call("check_command", 0x8C0000BC, h, st, 0, 1);
        if (r != 1 && r != 4) break;
    }
    printf("CMD_INIT pass %d result %d status %d %d %d %d\n", pass, r, (int)rd32(st), (int)rd32(st + 4), (int)rd32(st + 8), (int)rd32(st + 12));
    if (!(r == -1 && rd32(st) == 6)) break;
    }
    r = (int)call("check_drive", 0x8C0000BC, st, 0, 0, 4);
    printf("check_drive %d -> %d %d\n", r, (int)rd32(st), (int)rd32(st + 4));
    // sector mode (KOS cdrom_change_datatype): set 2048, data area, cdxa from drive
    B.wr((st + 0x100) & 0x1FFFFFFF, 4, 0); B.wr((st + 0x104) & 0x1FFFFFFF, 4, 0x1000); B.wr((st + 0x108) & 0x1FFFFFFF, 4, 1024);
    B.wr((st + 0x10C) & 0x1FFFFFFF, 4, 2048);
    r = (int)call("sector_mode", 0x8C0000BC, st + 0x100, 0, 0, 10);
    printf("sector_mode %d; GD reads %llu flash reads %llu\n", r, (unsigned long long)g_gdreads, (unsigned long long)B.flash_reads);
    return 0;
}
