// v1_fetch.sv -- I stage of the v1 core: fetch PC, one aligned 32-bit word per cycle, 4-entry instruction queue.
//
// The memory answers like an I-cache hit: the word at if_addr arrives in the same cycle and is in the queue at the next
// clock, so an instruction fetched in cycle c can issue in c + 1.  A word is fetched when it fits in the queue once this
// cycle's issues have left it; with two instructions per word and at most two issues per cycle, straight-line code
// never waits for fetch.  Timing effects of this stage (measured, see rtl/timing-proxy/README.md):
//   * a redirect target at an odd halfword is alone in its first fetch word, so it issues alone;
//   * a taken branch redirects in its last D cycle; the target word is fetched in the next cycle and issues one cycle
//     after that (2 cycles after the redirect);
//   * the first instruction fetched after a redirect is tagged: it never pairs as the second instruction of an issue
//     group (the pair logic only pairs instructions of the same fetch stream, so a delay slot never pairs with its
//     branch's target);
//   * LDC SR / LDC.L SR restart fetch at the next instruction (refetch), which then behaves as a branch target.
// Fetch runs ahead sequentially past a taken branch; the redirect flushes the queue behind the branch (and behind its
// delay slot).  When the delay slot has not been fetched yet, fetch fetches the slot's word first and takes only the
// slot from it, then the target.
module v1_fetch #(
  parameter int IQN = 4
) (
  input  logic        clk,
  input  logic        rst,
  input  logic [31:0] reset_pc,
  input  logic        halt,               // SLEEP: stop fetching
  output logic        if_req,
  output logic [31:0] if_addr,
  input  logic [31:0] if_data,
  // D-stage window
  output logic        q0_v, q1_v, q2_v,
  output logic [31:0] q0_pc, q1_pc,
  output logic [15:0] q0_op, q1_op,
  output logic        q0_tgt, q1_tgt,
  input  logic [1:0]  pop,                // entries D consumed this cycle
  // redirect: a taken branch in its last D cycle.  keep = the queue entry right after the popped ones is the delay slot
  // and stays; slot_pc = the slot's address when it is not in the queue yet (slot_pend)
  input  logic        redirect,
  input  logic [31:0] target,
  input  logic        keep,
  input  logic        slot_pend,
  input  logic [31:0] slot_pc,
  // refetch: flush everything, restart at rf_pc
  input  logic        refetch,
  input  logic [31:0] rf_pc
);
  localparam int CW = $clog2(IQN + 1);
  logic [31:0]    iq_pc [IQN];
  logic [15:0]    iq_op [IQN];
  logic [IQN-1:0] iq_tgt;
  logic [CW-1:0]  cnt;
  logic [31:0]    fpc;          // next fetch address (halfword aligned)
  logic           tgt_next;     // the next instruction fetched is a redirect target
  logic           slot_only;    // the next fetch is a pending delay slot: take only it, then go to pend_tgt
  logic [31:0]    pend_tgt;

  // this cycle's fetch
  logic           fetch;
  logic [1:0]     nfetch;       // instructions the word contributes (from fpc on)
  logic [CW-1:0]  after_pop;
  always_comb begin
    after_pop = cnt - CW'(pop);
    // on a redirect / refetch the queue behind the kept entries is dropped this cycle, and the fetch of this cycle is
    // discarded (it was sequential): the new stream starts next cycle
    nfetch = (fpc[1] || slot_only) ? 2'd1 : 2'd2;
    // a redirect whose delay slot is not queued yet fetches the slot in this same cycle (fetch is sequential, so fpc
    // is the slot's address), then the target next cycle; any other redirect discards this cycle's fetch
    fetch = !halt && !refetch && (!redirect || slot_pend) && (IQN - int'(after_pop)) >= 2;
    if_req = fetch;
    if_addr = {fpc[31:2], 2'b00};
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      cnt <= '0; iq_tgt <= '0; fpc <= reset_pc; tgt_next <= 1'b0; slot_only <= 1'b0; pend_tgt <= '0;
    end else begin
      logic [CW-1:0] c;
      c = after_pop;
      for (int k = 0; k < IQN; k++) begin
        int s; s = k + int'(pop);
        if (s < IQN) begin iq_pc[k] <= iq_pc[s]; iq_op[k] <= iq_op[s]; iq_tgt[k] <= iq_tgt[s]; end
        else iq_tgt[k] <= 1'b0;
      end
      if (refetch) begin
        cnt <= '0;
        fpc <= rf_pc;
        tgt_next <= 1'b0;            // a refetched instruction leads its group anyway
        slot_only <= 1'b0;
      end else if (redirect) begin
        if (slot_pend && fetch) begin
          // the slot comes in with this fetch, alone; the target follows
          iq_pc[0] <= fpc; iq_op[0] <= fpc[1] ? if_data[31:16] : if_data[15:0]; iq_tgt[0] <= 1'b0;
          cnt <= CW'(1);
          fpc <= target; slot_only <= 1'b0; tgt_next <= 1'b1;
        end else if (slot_pend) begin
          cnt <= '0;
          fpc <= slot_pc; slot_only <= 1'b1; pend_tgt <= target; tgt_next <= 1'b0;
        end else begin
          cnt <= keep ? CW'(1) : '0;   // the delay slot stays at the head
          fpc <= target; slot_only <= 1'b0; tgt_next <= 1'b1;
        end
      end else if (fetch) begin
        logic [15:0] lo, hi;
        lo = if_data[15:0];
        hi = if_data[31:16];
        if (fpc[1]) begin
          iq_pc[c] <= fpc; iq_op[c] <= hi; iq_tgt[c] <= tgt_next;
        end else begin
          iq_pc[c] <= fpc; iq_op[c] <= lo; iq_tgt[c] <= tgt_next;
          if (!slot_only) begin iq_pc[c + 1] <= fpc + 32'd2; iq_op[c + 1] <= hi; iq_tgt[c + 1] <= 1'b0; end
        end
        cnt <= c + CW'(nfetch);
        if (slot_only) begin
          fpc <= pend_tgt; slot_only <= 1'b0; tgt_next <= 1'b1;
        end else begin
          fpc <= {fpc[31:2], 2'b00} + 32'd4; tgt_next <= 1'b0;
        end
      end else begin
        cnt <= c;
      end
    end
  end

  assign q0_v = cnt >= 1;
  assign q1_v = cnt >= 2;
  assign q2_v = cnt >= 3;
  assign q0_pc = iq_pc[0];
  assign q1_pc = iq_pc[1];
  assign q0_op = iq_op[0];
  assign q1_op = iq_op[1];
  assign q0_tgt = iq_tgt[0];
  assign q1_tgt = iq_tgt[1];
endmodule
