// v1_bsc_basic.sv -- the BSC's basic-interface sequencer: normal memory (SRAM), burst ROM, byte-control SRAM, and the
// DRAM (EDO) and PCMCIA interfaces at transaction level (rtl/v1/spec/bsc-memmap.md 4.2, 4.3; hm 13.3.1-13.3.4,
// 13.3.6, 13.3.7, 13.3.9; SOC.md 3.3.3, 9.18).  Owner: WI-BSC.  Unused on the Dreamcast; selected by BCR1 / BCR2 / MD
// like the manual.  Bclk domain.
//
// Width splits (hm tables 13.11-13.13): an access wider than the bus becomes consecutive bus cycles ("pieces") of the
// bus width, lowest address first; a 32-byte access starts at the critical quadword and wraps in the line [P]; no
// arbitration between the pieces.  Every piece carries its own byte lanes (lane = address[2:0]); v1_bsc assembles
// the quadword a requester gets.  A DMAC quadword is one access (8 / 4 / 2 / 1 pieces on an 8 / 16 / 32 / 64-bit bus);
// a CPU quadword arrives as two L halves (SOC.md 3.3.2), each split by its own width.
//
// Per piece (a "group" start has T1; burst-ROM reads and DRAM put several pieces in one group):
//   SRAM / byte-control SRAM   T1, AnS0 setup, AnW waits {0 1 2 3 6 9 12 15}, T2 (the beat; RDY# sampled iff
//                              AnW != 0, repeated while RDY# is high), AnH hold               (WCR2, WCR3)
//   burst ROM (reads)          the first piece of a burst as SRAM, the next AnBST - 1 (4 8 16 32 accesses) after AnB
//                              pitch cycles (RDY# iff AnB != 0); writes as SRAM               (BCR1.AnBST, WCR2.AnB)
//   DRAM (transaction level)   RAS (T1), RCD - 1 {2 3 4 5}, per piece a CAS of AnW width {1 2 3 4 7 10 13 16}, then
//                              TPC {0..7} precharge; RDY# ignored [P]                          (MCR, WCR2)
//   PCMCIA (transaction level) TED {0..15}, T1, AnW + PCW {0 15 30 50} waits (up to 65), T2 (RDY# iff waits != 0),
//                              TEH (PCR; A5 fields for area 5, A6 for area 6 [P]: the BSC does not see PTEA.TC)
// A write commits at its first T1 [P].  `hold` while a beat waits for RDY#.
module v1_bsc_basic import v1_soc_pkg::*, v1_bsc_pkg::*; (
  input  logic                   clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / man / cpu: a manual reset lets a started cycle complete
  input  rst_t                   rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic                   bce,
  input  bregs_t                 regs,
  input  bx_t                    iss_x,
  output blen_t                  iss_len,        // bus Bcyc of the access at ISS (RDY# answered at once)
  output logic                   iss_ok,
  input  logic                   go,
  input  logic [1:0][3:0][63:0]  wbd,
  input  logic [1:0][3:0][7:0]   wbe,
  /* verilator lint_off UNUSEDSIGNAL */          // xi.rdata: v1_bsc takes the read data
  input  xb_in_t                 xi,
  /* verilator lint_on UNUSEDSIGNAL */
  output xb_out_t                xo,
  output bbeat_t                 bo,
  output logic                   commit,
  output logic                   commit_port,
  output logic                   hold
);
  localparam logic [2:0] PH_PRE = 3'd0, PH_T1 = 3'd1, PH_S0 = 3'd2, PH_WAIT = 3'd3, PH_BEAT = 3'd4, PH_POST = 3'd5;

  typedef struct packed {
    mt_e        mt;
    logic [5:0] np;                              // pieces
    logic [3:0] pb;                              // bytes per piece
    logic [5:0] grp;                             // pieces per group (a T1 each group)
    logic [3:0] pre, s0, post;
    logic [6:0] wf;                              // waits before the first piece of a group (PCMCIA: up to 15 + 50)
    logic [5:0] wn;                              // waits before a later piece of a group
    logic       rf, rn;                          // RDY# sampled on the first / a later piece
  } bp_t;

  /* verilator lint_off UNUSEDSIGNAL */          // whole records: the plan and the length read the fields they need
  function automatic bp_t bplan(input bx_t x);
    bp_t        p;
    logic [2:0] area, c;
    logic [3:0] wb, sb;
    logic [5:0] w;
    area = x.addr[28:26];
    p    = '0;
    p.mt = area_mt(regs, area);
    wb   = wid_bytes(area_wid(regs, area));
    sb   = x.size == BZ_LINE ? 4'd8 : sz_bytes(x.size);
    p.pb = sb < wb ? sb : wb;
    p.np = x.size == BZ_LINE ? 6'd32 / {2'd0, p.pb} : (sb > wb ? {2'd0, sb} / {2'd0, wb} : 6'd1);
    c    = aw_code(regs, area);
    p.grp = 6'd1;
    unique case (p.mt)
      MT_DRAM: begin
        p.grp  = p.np;
        p.wf   = {5'd0, regs.mcr[17:16]} + 7'd1;                              // RCD {2..5} - 1
        unique case (c)                                                       // CAS width {1 2 3 4 7 10 13 16} - 1
          3'd4:    w = 6'd6;
          3'd5:    w = 6'd9;
          3'd6:    w = 6'd12;
          3'd7:    w = 6'd15;
          default: w = {3'd0, c};
        endcase
        p.wf   = p.wf + {1'b0, w};
        p.wn   = w;
        p.post = {1'b0, regs.mcr[21:19]};                                     // TPC {0..7}
      end
      MT_PCM: begin
        logic [1:0] pcw;
        pcw   = area == 3'd5 ? regs.pcr[15:14] : regs.pcr[13:12];
        p.pre = w8(area == 3'd5 ? regs.pcr[11:9] : regs.pcr[8:6]);
        p.post = w8(area == 3'd5 ? regs.pcr[5:3] : regs.pcr[2:0]);
        p.wf  = {3'd0, w8(c)} + (pcw == 2'd0 ? 7'd0 : pcw == 2'd1 ? 7'd15 : pcw == 2'd2 ? 7'd30 : 7'd50);   // <= 65
        p.rf  = p.wf != 7'd0;
      end
      default: begin                                                          // SRAM, byte-control SRAM, burst ROM
        p.s0   = {3'd0, regs.wcr3[4 * area + 2]};
        p.post = {2'd0, regs.wcr3[4 * area +: 2]};
        p.wf   = {3'd0, w8(c)};
        p.rf   = c != 3'd0;
        if (p.mt == MT_BROM && !x.we) begin
          logic [2:0] bst, pitch;
          bst   = area == 3'd0 ? regs.bcr1[13:11] : area == 3'd5 ? regs.bcr1[10:8] : regs.bcr1[7:5];
          pitch = area == 3'd0 ? regs.wcr2[2:0] : area == 3'd5 ? regs.wcr2[22:20] : regs.wcr2[28:26];
          p.grp = bst == 3'd1 ? 6'd4 : bst == 3'd2 ? 6'd8 : bst == 3'd3 ? 6'd16 : 6'd32;
          p.wn  = {3'd0, pitch};
          p.rn  = pitch != 3'd0;
        end
      end
    endcase
    if (p.grp > p.np) p.grp = p.np;
    return p;
  endfunction

  // the bus length of an access (RDY# answered at once): up to 32 groups x 98 Bcyc (blen_t; an 8-bit line of 3-Bcyc
  // pieces is already 96, so 6 bits are not enough)
  function automatic blen_t blen(input bp_t p);
    blen_t ng, np, grp;
    np  = blen_t'(p.np);
    grp = blen_t'(p.grp);
    ng  = (np + grp - blen_t'(1)) / grp;
    return ng * (blen_t'(p.pre) + blen_t'(1) + blen_t'(p.s0) + blen_t'(p.wf) + blen_t'(1) + blen_t'(p.post)) +
           (np - ng) * (blen_t'(p.wn) + blen_t'(1));
  endfunction

  logic       bv;
  bx_t        bx;
  bp_t        bp;                                // bp.mt: the plan's
  /* verilator lint_on UNUSEDSIGNAL */
  logic [2:0] ph;
  logic [6:0] cnt;
  logic [5:0] pi;                                // piece index
  logic [5:0] gi;                                // piece index inside its group
  logic       dl1;                               // a quadword has been delivered already (crit = the first)

  bp_t ipl;
  always_comb ipl = bplan(iss_x);
  assign iss_len = blen(ipl);

  // the current piece: address, lanes, delivery
  logic [28:0] pa;
  logic [7:0]  pl;
  logic        pdlv, plast, gend;
  always_comb begin
    logic [4:0] off;
    off = '0;
    if (bx.size == BZ_LINE) begin
      off = {bx.addr[4:3], 3'b000} + 5'(pi * {2'd0, bp.pb});
      pa  = {bx.addr[28:5], off};
    end else pa = bx.addr + 29'(pi * {2'd0, bp.pb});
    pl    = 8'(({1'b0, 8'hFF} >> (4'd8 - bp.pb)) << pa[2:0]);
    plast = pi == bp.np - 6'd1;
    gend  = plast || gi == bp.grp - 6'd1;
    pdlv  = bx.size == BZ_LINE ? ({1'b0, pa[2:0]} + bp.pb == 4'd8) : (plast && !(bx.qh && !bx.qsec));
  end
  wire beat_rdy = ph == PH_BEAT && (!(gi == 6'd0 ? bp.rf : bp.rn) || xi.rdy);
  wire done_now = beat_rdy && plast && bp.post == 4'd0;
  assign iss_ok = !bv || done_now || (ph == PH_POST && plast && cnt == 7'd1);
  assign hold   = bv && ph == PH_BEAT && !beat_rdy;

  always_comb begin
    xo = '0; bo = '0; commit = 1'b0; commit_port = 1'b0;
    if (bv && ph == PH_T1) begin
      xo.start = 1'b1; xo.area = pa[28:26]; xo.addr = pa; xo.we = bx.we;
      xo.size  = bp.grp > 6'd1 ? size_code(bx.size) : (bp.pb == 4'd1 ? 3'd0 : bp.pb == 4'd2 ? 3'd1 : bp.pb == 4'd4 ? 3'd2
                                                                                                         : 3'd3);
      xo.lock  = bx.lock; xo.dack = bx.master == BM_DMA_SINGLE || bx.master == BM_DDT; xo.dack_id = bx.ch;
      if (bx.we && pi == 6'd0) begin commit = 1'b1; commit_port = bx.port; end
    end
    if (bv && ph == PH_BEAT) begin
      xo.beat  = 1'b1;
      xo.bidx  = pa[4:3];
      xo.baddr = {pa[28:3], 3'b000};
      xo.be    = bx.size == BZ_LINE ? pl : pl & lanes_of(bx.size, bx.addr[2:0]);
      if (bx.we) begin
        xo.be    = xo.be & wbe[bx.wb][pa[4:3]];
        xo.wdata = wbd[bx.wb][pa[4:3]];
      end
      xo.last  = beat_rdy && plast;
      if (beat_rdy) begin
        bo.v = 1'b1; bo.we = bx.we; bo.port = bx.port; bo.q = pa[4:3]; bo.lanes = xo.be; bo.baddr = xo.baddr;
        bo.wdata = xo.wdata; bo.dack = bx.master == BM_DMA_SINGLE || bx.master == BM_DDT;
        bo.dlv = pdlv && !bx.we; bo.crit = pdlv && !dl1; bo.last = plast;
      end
    end
  end

  always_ff @(posedge clk) begin
    if (rst.por) begin
      bv <= 1'b0; bx <= '0; bp <= '0; ph <= PH_PRE; cnt <= '0; pi <= '0; gi <= '0; dl1 <= 1'b0;
    end else if (bce) begin
      if (bv) begin
        unique case (ph)
          PH_PRE: begin
            if (cnt <= 7'd1) ph <= PH_T1;
            cnt <= cnt - 7'd1;
          end
          PH_T1: begin
            if (bp.s0 != 4'd0)      begin ph <= PH_S0;   cnt <= {3'd0, bp.s0}; end
            else if (bp.wf != 7'd0) begin ph <= PH_WAIT; cnt <= bp.wf; end
            else ph <= PH_BEAT;
          end
          PH_S0: begin
            if (cnt == 7'd1) begin
              if (bp.wf != 7'd0) begin ph <= PH_WAIT; cnt <= bp.wf; end
              else ph <= PH_BEAT;
            end else cnt <= cnt - 7'd1;
          end
          PH_WAIT: begin
            if (cnt == 7'd1) ph <= PH_BEAT;
            cnt <= cnt - 7'd1;
          end
          PH_BEAT: begin
            if (beat_rdy) begin
              if (pdlv) dl1 <= 1'b1;
              if (gend && bp.post != 4'd0) begin ph <= PH_POST; cnt <= {3'd0, bp.post}; end
              else if (plast) bv <= 1'b0;
              else begin
                pi <= pi + 6'd1;
                if (gend) begin
                  gi <= '0;
                  if (bp.pre != 4'd0) begin ph <= PH_PRE; cnt <= {3'd0, bp.pre}; end else ph <= PH_T1;
                end else begin
                  gi <= gi + 6'd1;
                  if (bp.wn != 6'd0) begin ph <= PH_WAIT; cnt <= {1'b0, bp.wn}; end
                end
              end
            end
          end
          default: begin                                                    // PH_POST
            if (cnt == 7'd1) begin
              if (plast) bv <= 1'b0;
              else begin
                pi <= pi + 6'd1; gi <= '0;
                if (bp.pre != 4'd0) begin ph <= PH_PRE; cnt <= {3'd0, bp.pre}; end else ph <= PH_T1;
              end
            end else cnt <= cnt - 7'd1;
          end
        endcase
      end
      if (go) begin
        bv <= 1'b1; bx <= iss_x; bp <= ipl; pi <= '0; gi <= '0; dl1 <= 1'b0;
        if (ipl.pre != 4'd0) begin ph <= PH_PRE; cnt <= {3'd0, ipl.pre}; end else ph <= PH_T1;
      end
    end
  end
endmodule
