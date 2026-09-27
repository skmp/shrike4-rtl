// v1_bsc_mpx.sv -- the MPX bus sequencer (area 0 with A0MPX, areas 1-6 with MEMMPX; on the DC every area but 3 and 7)
// (rtl/v1/spec/bsc-memmap.md 4.3, 8.5.3; hm 13.3.8 Figures 13.52-13.59; SOC.md 3.3.2).  Owner: WI-BSC.  Bclk domain.
//
//   Tm1       the address / size cycle (xb.start: area, addr, size code 0 B 1 W 2 L 3 Q 4 burst on D63-D61)
//   Tmd1w     the first-data waits of WCR2.AnW: read 1 1 2 3, write 0 1 2 3 for codes 000-011 (100-111 the same)
//   Tmd1      the data cycle: the beat is presented (xb.beat) and RDY# is sampled; while the board holds RDY# off the
//             cycle repeats (the board's device wait count, board.md) -- `hold` freezes v1_bsc_arb's bus bookkeeping
//   Tmd2..4   a burst's later beats, each after the later-data waits (0 for codes 0xx, 1 for 1xx) and RDY#
// A 32-byte access is one burst of 4 beats on a 64-bit bus (8 on a 32-bit bus); B / W / L is one beat (a CPU quadword
// arrives here as two L halves, SOC.md 3.3.2).  A DMAC quadword is not split: one Tm1 with size code Q and one beat of
// eight lanes on a 64-bit bus, two beats (low, high longword; the later-data waits between them, like a burst's) on a
// 32-bit bus [P: the manual gives the size code, not the 32-bit data cycles].  A write commits at its Tm1 [P] (the data
// is in the BSC's buffer; its requester retires the write, v1_bsc).
// What it makes on the DC (A0W = 3, no RDY wait for the SB registers): the read beat at b7 (Tm1 b3, waits b4-b6), one
// Bcyc after the SDRAM row-hit beat: SB register read 18 Icyc back to back, 17 isolated [HW bsc-memmap 4.7]; the
// minimum idle after a read before the next MPX address (WCR1 "M (1)", v1_bsc_pkg idle_cyc) keeps SB reads 1 Bcyc
// apart on the bus.  An SB write is Tm1 + 3 waits + Tmd1 = 5 Bcyc; with the one idle Bcyc between two MPX writes
// (idle_cyc [P]) a write stream is 12 Icyc per write [HW SB registers, the first texture-memory writes], while an SB
// read -> write pair stays 28 and SB write -> RAM read 28 [HW bscmm2] (a board RDY# wait on writes would make those 30).
module v1_bsc_mpx import v1_soc_pkg::*, v1_bsc_pkg::*; (
  input  logic                   clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / man / cpu: a manual reset lets a started cycle complete
  input  rst_t                   rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic                   bce,
  input  bregs_t                 regs,
  input  bx_t                    iss_x,
  output logic [5:0]             iss_len,
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
  localparam logic [1:0] PH_TM1 = 2'd0, PH_WAIT = 2'd1, PH_DATA = 2'd2;

  logic       mv;
  bx_t        mx;
  logic [1:0] ph;
  logic [3:0] wc;
  logic [2:0] j;
  logic [3:0] nb;
  logic [1:0] w1;
  logic       w2, b32;

  // the plan of an access: waits and beats
  /* verilator lint_off UNUSEDSIGNAL */          // a whole access record: its area, size and direction
  function automatic logic [3:0] mbeats(input bx_t x);            // data cycles: a line 4 / 8, a DMAC Q 1 / 2, else 1
    logic b32w;
    b32w = area_wid(regs, x.addr[28:26]) == 2'd3;
    return x.size == BZ_LINE ? (b32w ? 4'd8 : 4'd4) : (x.size == BZ_Q && b32w) ? 4'd2 : 4'd1;
  endfunction
  function automatic logic [5:0] mlen(input bx_t x);
    logic [2:0] c;
    logic [3:0] n;
    c = aw_code(regs, x.addr[28:26]);
    n = mbeats(x);
    return 6'd2 + {4'd0, mpx_w1(c, x.we)} + ({2'd0, n} - 6'd1) * (c[2] ? 6'd2 : 6'd1);
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  wire last_beat = ph == PH_DATA && {1'b0, j} == nb - 4'd1;
  assign iss_len = mlen(iss_x);
  assign iss_ok  = !mv || (last_beat && xi.rdy);
  assign hold    = mv && ph == PH_DATA && !xi.rdy;

  always_comb begin
    logic       line, qw;
    logic [1:0] q;
    xo = '0; bo = '0; commit = 1'b0; commit_port = 1'b0;
    line = mx.size == BZ_LINE;
    qw   = mx.size == BZ_Q && b32;                                           // a DMAC Q on a 32-bit bus: two beats
    q = line ? mx.addr[4:3] + (b32 ? 2'(j[2:1]) : j[1:0]) : mx.addr[4:3];
    if (mv && ph == PH_TM1) begin
      xo.start = 1'b1; xo.area = mx.addr[28:26]; xo.addr = mx.addr; xo.we = mx.we; xo.size = size_code(mx.size);
      xo.lock = mx.lock; xo.dack = mx.master == BM_DMA_SINGLE || mx.master == BM_DDT; xo.dack_id = mx.ch;
      if (mx.we) begin commit = 1'b1; commit_port = mx.port; end
    end
    if (mv && ph == PH_DATA) begin
      xo.beat  = 1'b1;
      xo.bidx  = line ? (b32 ? 2'(j[2:1]) : j[1:0]) : 2'd0;
      xo.baddr = line ? {mx.addr[28:5], q, 3'b000} : {mx.addr[28:3], 3'b000};
      xo.be    = (line || qw) ? (b32 ? (j[0] ? 8'hF0 : 8'h0F) : 8'hFF) : lanes_of(mx.size, mx.addr[2:0]);
      if (mx.we) begin
        xo.be    = xo.be & wbe[mx.wb][q];
        xo.wdata = wbd[mx.wb][q];
      end
      xo.last  = last_beat && xi.rdy;
      if (xi.rdy) begin
        bo.v = 1'b1; bo.we = mx.we; bo.port = mx.port; bo.q = q; bo.lanes = xo.be; bo.baddr = xo.baddr;
        bo.wdata = xo.wdata; bo.dack = xo.dack || mx.master == BM_DMA_SINGLE || mx.master == BM_DDT;
        if (line) begin
          bo.dlv = b32 ? j[0] : 1'b1; bo.crit = b32 ? j == 3'd1 : j == 3'd0; bo.last = last_beat;
        end else begin
          bo.last = last_beat;                                                // one beat, or a DMAC Q's second
          bo.dlv = last_beat && !(mx.qh && !mx.qsec); bo.crit = bo.dlv;
        end
      end
    end
  end

  always_ff @(posedge clk) begin
    if (rst.por) begin
      mv <= 1'b0; mx <= '0; ph <= PH_TM1; wc <= '0; j <= '0; nb <= '0; w1 <= '0; w2 <= 1'b0; b32 <= 1'b0;
    end else if (bce) begin
      if (mv) begin
        unique case (ph)
          PH_TM1: begin
            if (w1 != 2'd0) begin ph <= PH_WAIT; wc <= {2'd0, w1}; end
            else ph <= PH_DATA;
          end
          PH_WAIT: begin
            wc <= wc - 4'd1;
            if (wc == 4'd1) ph <= PH_DATA;
          end
          default: begin                                                    // PH_DATA
            if (xi.rdy) begin
              if (last_beat) mv <= 1'b0;
              else begin
                j <= j + 3'd1;
                if (w2) begin ph <= PH_WAIT; wc <= 4'd1; end
              end
            end
          end
        endcase
      end
      if (go) begin
        logic [2:0] c;
        c   = aw_code(regs, iss_x.addr[28:26]);
        mv  <= 1'b1; mx <= iss_x; ph <= PH_TM1; j <= '0;
        w1  <= mpx_w1(c, iss_x.we);
        w2  <= c[2];
        b32 <= area_wid(regs, iss_x.addr[28:26]) == 2'd3;
        nb  <= mbeats(iss_x);
      end
    end
  end
endmodule
