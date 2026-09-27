// v1_bsc_pkg.sv -- WI-BSC's own types and the BSC register-field decoders shared by v1_biu, v1_bsc and its sub-modules
// (rtl/v1/SOC.md 3.3, 3.8; rtl/v1/spec/bsc-memmap.md 3, 4, 8).  Owner: WI-BSC.  The contract types (bq_t, bs_t, bwd_t,
// breq_t, xb_out_t, xb_in_t, p4s_*) are v1_soc_pkg's; nothing here changes them.
//
// Timing conventions of every BSC module: one clock (Iclk); ce.bce marks the last Icyc of a Bclk period and every BSC
// flop updates only at the end of a bce cycle, except the write-data capture of v1_bsc (the Icyc after wd_req,
// SOC.md 3.3.2).  "Bcyc b<k>" in comments counts Bclk periods from a request's accept (ready = b0).
package v1_bsc_pkg;
  import v1_soc_pkg::*;

  // ---- v1_biu's requester indices (SOC.md 3.3's order; identical to tb/v1_bsc_stub.sv's BRQ_*)
  localparam int BIU_IC_FILL = 0;
  localparam int BIU_IC_UNC  = 1;
  localparam int BIU_OC_FILL = 2;
  localparam int BIU_OC_WBB  = 3;
  localparam int BIU_OC_WTB  = 4;
  localparam int BIU_OC_UNC  = 5;
  localparam int BIU_SQ      = 6;
  localparam int BIU_N       = 7;

  // ---- the memory type of an area (BCR1 / BCR2 / MCR / MD, hm 13.2.1-13.2.2; bsc-memmap 2.2)
  typedef enum logic [2:0] {
    MT_A7    = 3'd0,                 // physical area 7: no external cycle (C16), answered by v1_bsc_sdram's phantom path
    MT_SDRAM = 3'd1,                 // areas 2 / 3 with DRAMTP 010 / 011 (DC: area 3)
    MT_MPX   = 3'd2,                 // area 0 with A0MPX, areas 1-6 with MEMMPX (DC: 0, 1, 2, 4, 5, 6)
    MT_SRAM  = 3'd3,                 // normal memory (basic interface)
    MT_BROM  = 3'd4,                 // burst ROM (areas 0, 5, 6 with AnBST != 0; reads burst, writes as SRAM)
    MT_BCS   = 3'd5,                 // byte-control SRAM (areas 1, 4 with AnMBC)
    MT_DRAM  = 3'd6,                 // EDO DRAM (areas 2 / 3 with DRAMTP 100 / 101), transaction level
    MT_PCM   = 3'd7                  // PCMCIA (areas 5, 6 with A56PCM), transaction level
  } mt_e;

  // ---- SDRAM command codes on xb_sd_cmd (the side band; the board / the protocol checker decode them) [P]
  localparam logic [3:0] SDC_NOP   = 4'd0;   // a dummy cycle (the Tnop before a row-hit write, Figure 13.34)
  localparam logic [3:0] SDC_ACTV  = 4'd1;
  localparam logic [3:0] SDC_READ  = 4'd2;
  localparam logic [3:0] SDC_READA = 4'd3;
  localparam logic [3:0] SDC_WRIT  = 4'd4;
  localparam logic [3:0] SDC_WRITA = 4'd5;
  localparam logic [3:0] SDC_PRE   = 4'd6;
  localparam logic [3:0] SDC_PALL  = 4'd7;
  localparam logic [3:0] SDC_REF   = 4'd8;
  localparam logic [3:0] SDC_SELF  = 4'd9;
  localparam logic [3:0] SDC_MRS   = 4'd10;

  // ---- the BSC configuration registers as the sequencers see them (v1_bsc_regs holds them)
  typedef struct packed {
    logic [31:0] bcr1;
    logic [15:0] bcr2;
    logic [31:0] wcr1;
    logic [31:0] wcr2;
    logic [31:0] wcr3;
    logic [31:0] mcr;
    logic [15:0] pcr;
  } bregs_t;

  // ---- one bus access as it travels the front end and a sequencer (a CPU Q travels as two BZ_L halves, a DMAC Q as
  //      one BZ_Q access, SOC.md 3.3.2)
  typedef struct packed {
    logic        port;               // 0 the CPU port (v1_biu), 1 the DMAC port
    logic        we;
    logic [28:0] addr;               // first byte address (BZ_LINE: the critical quadword; a Q half: its longword)
    bsz_e        size;               // BZ_B / W / L / LINE; BZ_Q only from the DMAC port (8-byte aligned)
    logic        lock;
    bmaster_e    master;
    logic [1:0]  ch;
    logic        qh;                 // one half of a split CPU Q (SOC.md 3.3.2)
    logic        qsec;               // the second half: it delivers the quadword / retires the write
    logic        wb;                 // the write-data buffer this write's beats were pulled into
  } bx_t;

  // ---- a data beat that completed this Bcyc, as the requester side sees it (every sequencer drives one; ORed)
  typedef struct packed {
    logic        v;                  // a data beat completed (the board answered, or no RDY# sampled, or area 7)
    logic        we;
    logic        port;
    logic        dlv;                // read: deliver the assembled quadword now (rd_v); else only collect the lanes
    logic [1:0]  q;                  // rd_beat: the quadword index in the line
    logic        crit;               // the access's critical (first delivered) beat
    logic        last;               // the access's last delivered beat (rd_last / done)
    logic [7:0]  lanes;              // the byte lanes this beat carries (little endian: lane = address[2:0])
    logic        phantom;            // area 7: no external cycle, data 0
    logic        dack;               // single-address DMA
    logic [28:0] baddr;              // the beat's 8-byte-aligned address
    logic [63:0] wdata;              // a write beat's data (the DMAW record)
  } bbeat_t;

  // ---- the last bus access: what WCR1's idle rule and the overlap rules compare the next one with
  typedef struct packed {
    logic        v;                  // an external access (not a refresh / command / DTR slot)
    logic [2:0]  area;
    mt_e         mt;
    logic        we;
    logic        dsingle;            // single-address DMA (DACK / TDACK)
    logic        port;
    logic        sd_rd;              // an SDRAM read in RAS-down mode: the next read of its area may overlap its tail
    logic        sd_ln;              // ... a line read of the CPU port: the port's next write of another line may start
                                     // in its last data cycle too (M3: v1_bsc_arb's write-after-line rule)
    logic [28:5] la;                 // the access's line (the write-after-line rule excludes a write of that line [P])
    logic [2:0]  credit;             // the DQM-masked beats that end it (a single SDRAM write): empty data-bus cycles
  } brec_t;

  // ==================================================================================================================
  // register-field decoders (hm 13.2; bsc-memmap 3.2)
  // ==================================================================================================================

  // WCR1 idle / WCR2 wait code -> cycles: 000 0, 001 1, 010 2, 011 3, 100 6, 101 9, 110 12, 111 15
  function automatic logic [3:0] w8(input logic [2:0] c);
    unique case (c)
      3'd0: return 4'd0;   3'd1: return 4'd1;   3'd2: return 4'd2;   3'd3: return 4'd3;
      3'd4: return 4'd6;   3'd5: return 4'd9;   3'd6: return 4'd12;  default: return 4'd15;
    endcase
  endfunction

  function automatic mt_e area_mt(input bregs_t r, input logic [2:0] area);
    logic       mpx;
    logic [2:0] tp;
    mpx = r.bcr1[17];                                    // MEMMPX
    tp  = r.bcr1[4:2];                                   // DRAMTP (001 / 110 / 111 reserved: as 000 [P])
    unique case (area)
      3'd0: return r.bcr1[29] ? MT_MPX : (r.bcr1[13:11] != 3'd0 ? MT_BROM : MT_SRAM);
      3'd1: return mpx ? MT_MPX : (r.bcr1[21] ? MT_BCS : MT_SRAM);
      3'd2: return tp == 3'b011 ? MT_SDRAM : tp == 3'b101 ? MT_DRAM : (mpx ? MT_MPX : MT_SRAM);
      3'd3: return (tp == 3'b010 || tp == 3'b011) ? MT_SDRAM : (tp == 3'b100 || tp == 3'b101) ? MT_DRAM
                                                                                              : (mpx ? MT_MPX : MT_SRAM);
      3'd4: return mpx ? MT_MPX : (r.bcr1[20] ? MT_BCS : MT_SRAM);
      3'd5: return r.bcr1[0] ? MT_PCM : mpx ? MT_MPX : (r.bcr1[10:8] != 3'd0 ? MT_BROM : MT_SRAM);
      3'd6: return r.bcr1[0] ? MT_PCM : mpx ? MT_MPX : (r.bcr1[7:5] != 3'd0 ? MT_BROM : MT_SRAM);
      default: return MT_A7;
    endcase
  endfunction

  // bus width code of an area: 0 64-bit, 1 8-bit, 2 16-bit, 3 32-bit (the BCR2 encoding)
  function automatic logic [1:0] area_wid(input bregs_t r, input logic [2:0] area);
    mt_e        mt;
    logic [1:0] sz;
    mt = area_mt(r, area);
    if (mt == MT_SDRAM || mt == MT_DRAM) begin          // MCR.SZ has priority (00 64, 11 32, 10 16 DRAM, 01 reserved)
      unique case (r.mcr[8:7])
        2'b11:   return 2'd3;
        2'b10:   return mt == MT_DRAM ? 2'd2 : 2'd3;       // 16-bit SDRAM does not exist: as 32 [P]
        default: return 2'd0;                              // 01 reserved: as 64 [P]
      endcase
    end
    sz = area == 3'd0 ? r.bcr2[15:14] : r.bcr2[2 * area +: 2];
    if (mt == MT_MPX && (sz == 2'd1 || sz == 2'd2)) return 2'd3;   // MPX is 32 or 64 bits only (hm 13.3.8) [P]
    return sz;                                           // 00 with PORTEN = 1 is "reserved": the DC does it, 64 [HW]
  endfunction

  function automatic logic [3:0] wid_bytes(input logic [1:0] w);
    unique case (w)
      2'd1:    return 4'd1;
      2'd2:    return 4'd2;
      2'd3:    return 4'd4;
      default: return 4'd8;
    endcase
  endfunction

  function automatic logic [3:0] sz_bytes(input bsz_e s);
    unique case (s)
      BZ_B:    return 4'd1;
      BZ_W:    return 4'd2;
      BZ_L:    return 4'd4;
      default: return 4'd8;
    endcase
  endfunction

  // the byte lanes of a B / W / L / Q transfer at a (a line beat: all eight)
  function automatic logic [7:0] lanes_of(input bsz_e s, input logic [2:0] a);
    unique case (s)
      BZ_B:    return 8'h01 << a;
      BZ_W:    return 8'h03 << {a[2:1], 1'b0};
      BZ_L:    return 8'h0F << {a[2], 2'b00};
      default: return 8'hFF;
    endcase
  endfunction

  // ---- bus occupancy in Bcyc.  A basic-interface access can hold the bus far longer than an SDRAM / MPX one: an 8-bit
  //      line is 32 pieces, and a PCMCIA piece is up to TED 15 + T1 + 65 waits + T2 + TEH 15 = 97 Bcyc, so a line is up
  //      to 3104 Bcyc.  blen_t carries an access's length; brel_t is v1_bsc_arb's `rel` (last busy Bcyc - now, signed).
  localparam int BL_W = 12;
  typedef logic [BL_W-1:0]      blen_t;
  typedef logic signed [BL_W:0] brel_t;
  function automatic brel_t brel(input blen_t v); return brel_t'({1'b0, v}); endfunction

  // the MPX / xb size code (D63..D61): 0 B, 1 W, 2 L, 3 Q, 4 32-byte burst
  function automatic logic [2:0] size_code(input bsz_e s);
    unique case (s)
      BZ_B:    return 3'd0;
      BZ_W:    return 3'd1;
      BZ_L:    return 3'd2;
      BZ_Q:    return 3'd3;
      default: return 3'd4;
    endcase
  endfunction

  // ---- SDRAM timing fields (MCR, WCR2; hm 13.2.4, 13.2.8)
  function automatic logic [3:0] sd_cl(input bregs_t r, input logic [2:0] area);   // CAS latency
    logic [2:0] c;
    c = area == 3'd2 ? r.wcr2[11:9] : r.wcr2[15:13];
    unique case (c)
      3'd0:    return 4'd1;                              // inhibited: as 1 [P]
      3'd1:    return 4'd1;
      3'd2:    return 4'd2;
      3'd3:    return 4'd3;
      3'd4:    return 4'd4;
      default: return 4'd5;                              // 101 = 5; 110 / 111 inhibited: as 5 [P]
    endcase
  endfunction
  function automatic logic [3:0] sd_tpc(input bregs_t r);  return {1'b0, r.mcr[21:19]} + 4'd1; endfunction   // 1-8
  function automatic logic [3:0] sd_rcd(input bregs_t r);                                                     // 2-4
    return r.mcr[17:16] == 2'd0 ? 4'd2 : {2'b00, r.mcr[17:16]} + 4'd1;                   // 00 reserved: as 2 [P]
  endfunction
  function automatic logic [3:0] sd_trwl(input bregs_t r);                                                    // 1-5
    return r.mcr[15:13] > 3'd4 ? 4'd5 : {1'b0, r.mcr[15:13]} + 4'd1;                     // 101-111 reserved: 5 [P]
  endfunction
  // no ACTV for TRAS + TRC after REF: TRAS {4..11} + TRC {0, 3, 6, ... 21}  (DC: 3 + 4 + 0 = 7)
  function automatic logic [5:0] sd_trfc(input bregs_t r);
    return {3'b000, r.mcr[12:10]} + 6'd4 + {3'b000, r.mcr[29:27]} * 6'd3;
  endfunction

  // SDRAM address multiplexing (MCR AMX / AMXEXT / SZ, hm 13.2.8 and Appendix F): bank and row of a physical address
  function automatic logic [1:0] sd_bank(input bregs_t r, input logic [28:0] a);
    logic b64, ext;
    b64 = r.mcr[8:7] != 2'b11;
    ext = r.mcr[6];
    unique case (r.mcr[5:3])
      3'd0:    return {1'b0, b64 ? (ext ? a[21] : a[22]) : (ext ? a[20] : a[21])};
      3'd1:    return {1'b0, b64 ? (ext ? a[22] : a[23]) : (ext ? a[21] : a[22])};
      3'd2:    return b64 ? a[24:23] : a[23:22];
      3'd3:    return b64 ? a[25:24] : a[24:23];
      3'd5:    return {1'b0, b64 ? a[23] : a[22]};
      3'd7:    return {1'b0, b64 ? a[21] : a[20]};
      default: return b64 ? a[23:22] : a[22:21];          // 4 (the DC: bank = a[23:22]); 6 reserved: as 4 [P]
    endcase
  endfunction
  function automatic logic [12:0] sd_row(input bregs_t r, input logic [28:0] a);
    logic b64, ext;
    b64 = r.mcr[8:7] != 2'b11;
    ext = r.mcr[6];
    unique case (r.mcr[5:3])
      3'd0:    return b64 ? (ext ? {2'b0, a[22], a[20:11]} : {2'b0, a[21:11]})
                          : (ext ? {2'b0, a[21], a[19:10]} : {2'b0, a[20:10]});
      3'd1:    return b64 ? (ext ? {2'b0, a[23], a[21:12]} : {2'b0, a[22:12]})
                          : (ext ? {2'b0, a[22], a[20:11]} : {2'b0, a[21:11]});
      3'd2:    return b64 ? {1'b0, a[22:11]} : {1'b0, a[21:10]};
      3'd3:    return b64 ? {1'b0, a[23:12]} : {1'b0, a[22:11]};
      3'd5:    return b64 ? {1'b0, a[22:11]} : {1'b0, a[21:10]};
      3'd7:    return b64 ? {3'b0, a[20:11]} : {3'b0, a[19:10]};
      default: return b64 ? {2'b0, a[21:11]} : {2'b0, a[20:10]};   // 4: the DC, row = a[21:11]
    endcase
  endfunction

  // ---- MPX waits (WCR2 AnW, hm 13.2.4 "When MPX is Used"): first data 1 1 2 3 (read) / 0 1 2 3 (write); later data
  //      0 for codes 0xx, 1 for 1xx
  function automatic logic [2:0] aw_code(input bregs_t r, input logic [2:0] area);
    unique case (area)
      3'd0:    return r.wcr2[5:3];
      3'd1:    return r.wcr2[8:6];
      3'd2:    return r.wcr2[11:9];
      3'd3:    return r.wcr2[15:13];
      3'd4:    return r.wcr2[19:17];
      3'd5:    return r.wcr2[25:23];
      default: return r.wcr2[31:29];
    endcase
  endfunction
  function automatic logic [1:0] mpx_w1(input logic [2:0] c, input logic we);
    return (!we && c[1:0] == 2'd0) ? 2'd1 : c[1:0];
  endfunction

  // ---- WCR1's idle cycles between two accesses (hm 13.2.3 table "Idle Insertion between Accesses", 13.3.10).  The
  //      count is the setting minus the empty cycles already there; a single SDRAM write's DQM-masked beats count as
  //      empty (brec_t.credit; v1_bsc_arb) [P: RAM write -> SB read 28 Icyc per pair, bsc-memmap 6.4], and so do the
  //      Bcyc of a refresh / SDRAM mode command / PALL run between the two (no data bus; v1_bsc_arb's rel_d)
  function automatic logic [3:0] idle_cyc(input bregs_t r, input brec_t p, input logic [2:0] n_area, input mt_e n_mt,
                                         input logic n_we, input logic n_dsingle);
    logic [3:0] m, d, m1, d1;
    logic       same, nmpx;
    if (!p.v) return 4'd0;
    m    = w8(r.wcr1[4 * p.area +: 3]);                  // AnIW of the preceding access's area
    d    = w8(r.wcr1[30:28]);                            // DMAIW
    m1   = m == 4'd0 ? 4'd1 : m;                         // "M (1)": at least one before an MPX address output
    d1   = d == 4'd0 ? 4'd1 : d;
    same = p.area == n_area;
    nmpx = n_mt == MT_MPX;
    if (p.dsingle && p.we) begin                         // DMA write (device -> memory): DMAIW before everything,
      if (same && n_dsingle && n_we) return 4'd0;        // except consecutive accesses to the same device ("*")
      return (nmpx && !same) ? d1 : d;
    end
    if (!p.we) begin                                     // a read (CPU, DMA dual, DMA single memory -> device)
      if (nmpx) return m1;
      if (!same) return m;
      return n_we ? m : 4'd0;
    end
    // a write, then the same area: none, except between two MPX writes, which get the "M (1)" of an MPX address after
    // a write to another area -- at least one, AnIW when that is larger [P: the manual's table gives none; measured
    // only with AnIW = 1: SB register and texture-memory write streams 12 Icyc per write, i.e. one idle Bcyc, while an
    // SB read -> write pair is 28 and SB write -> read 0 idle, bsc-memmap 4.7.  AnIW > 1 is the generalisation]
    if (same) return (nmpx && n_we) ? m1 : 4'd0;
    return nmpx ? m1 : m;
  endfunction

endpackage
