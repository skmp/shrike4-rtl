// tp_fetch.sv -- I stage of the timing proxy: instruction fetch and the instruction queue.
//
// The I-cache delivers one aligned 32-bit word (two instructions) per cycle; with everything cached a fetch never
// misses.  Fetched instructions go into a small queue whose two oldest entries are the D stage's decode window
// (tp_core); an instruction fetched in cycle c can issue in c + 1.  The proxy is trace driven: the trace port shows
// the next two instructions in program order and the fetch unit takes the ones that lie in the word it fetches (a word
// holds the instruction at pc and, when pc is the even half, the one after it).
//
// Branches: fetch is sequential.  After a taken branch the fetch unit still fetches the delay slot of a delayed branch,
// then nothing until the branch resolves (redirect from tp_core in the branch's last D cycle); the target word is
// fetched in the next cycle, so the target is in D two cycles after the redirect.  The words the real I stage fetches
// past the branch meanwhile are discarded there, so here they are simply not taken from the trace.  The first
// instruction after a redirect is marked (tgt): it never pairs with an older instruction (the delay slot never
// co-issues with the target), and a target at an odd halfword is alone in its first fetch word.
//
// Refetch (LDC SR / LDC.L SR): the instructions behind it are fetched again, one word per cycle starting the cycle
// after the refetch request (the queue already holds them: they are re-marked ready word by word).
//
// Freeze (tp_core: LDTLB, OCBI, OCBP, a memory access right behind OCBWB): the whole pipeline stops, the I stage too.
// Nothing is fetched or re-marked, so a redirect raised just before the freeze fetches its target after it (a taken
// BT / BF paired with OCBI, or OCBI in a delay slot, loses the full 2 cycles).
module tp_fetch import tp_pkg::*; #(
  parameter int IQN = 4                 // queue entries (instructions)
) (
  input  logic       clk,
  input  logic       rst,
  // trace window
  input  logic       t0_v, t1_v,
  input  tr_t        t0, t1,
  output logic [1:0] take,
  // D-stage window
  output logic       q0_v, q1_v,
  output tr_t        q0, q1,
  output logic       q0_tgt, q1_tgt,
  input  logic [1:0] pop,               // entries the D stage consumed this cycle
  input  logic       redirect,          // the outstanding taken branch resolved: fetch its target next cycle
  input  logic       refetch,           // refetch everything in the queue (LDC SR)
  input  logic       freeze             // whole-pipeline freeze (tp_core): the I stage holds as well
);
  localparam int CW = $clog2(IQN + 1);
  tr_t          iq  [IQN];
  logic [IQN-1:0] rdy, tgt;
  logic [CW-1:0]  cnt;

  // predecode: BT / BF have no delay slot
  function automatic logic nodelay(input logic [15:0] op);
    return op[15:8] == 8'h89 || op[15:8] == 8'h8B;
  endfunction
  // predecode: the forms that transfer control (the trace's taken bit means nothing on anything else)
  function automatic logic is_br(input logic [15:0] op);
    return op[15:12] == 4'hA || op[15:12] == 4'hB ||                                   // BRA BSR
           op[15:8] == 8'h89 || op[15:8] == 8'h8B || op[15:8] == 8'h8D || op[15:8] == 8'h8F ||   // BT BF BT/S BF/S
           (op[15:12] == 4'h0 && (op[7:0] == 8'h23 || op[7:0] == 8'h03)) ||              // BRAF BSRF
           (op[15:12] == 4'h4 && (op[7:0] == 8'h2B || op[7:0] == 8'h0B)) ||              // JMP JSR
           op == 16'h000B || op == 16'h002B;                                            // RTS RTE
  endfunction
  wire tk0 = t0.tk && is_br(t0.op);
  wire tk1 = t1.tk && is_br(t1.op);

  logic stopped;        // waiting for the redirect of a taken branch
  logic slot_due;       // a taken delayed branch was fetched, its slot not yet
  logic early;          // the redirect arrived before the branch's sequential part was fetched
  logic after_rd;       // the next word fetched is a redirect target

  // ---- refetch: the oldest not-ready entry (and its word mate) becomes ready; when the mate is not in the queue
  //      yet, it comes from the trace in the same fetch
  logic [IQN-1:0] rf_mark;
  logic           rf_any, rf_mate_tr;
  always_comb begin
    rf_mark = '0;
    rf_any = 1'b0;
    rf_mate_tr = 1'b0;
    for (int k = 0; k < IQN; k++) begin
      if (!freeze && !rf_any && k < int'(cnt) && !rdy[k]) begin
        rf_any = 1'b1;
        rf_mark[k] = 1'b1;
        if (k + 1 < int'(cnt)) begin
          if (!iq[k].pc[1] && iq[k + 1].pc == iq[k].pc + 32'd2) rf_mark[k + 1] = 1'b1;
        end else begin
          rf_mate_tr = !iq[k].pc[1] && t0_v && t0.pc == iq[k].pc + 32'd2;
        end
      end
    end
  end

  // ---- what this cycle's trace fetch takes: a whole word normally, only the second half of a refetch word
  logic same_word, can, stop_after;
  always_comb begin
    logic one;
    one = rf_any;
    same_word = !one && t1_v && !t0.pc[1] && t1.pc == t0.pc + 32'd2;
    // a word is fetched when it fits once this cycle's issues have left the queue
    can = t0_v && !freeze && !stopped && !refetch && (one ? rf_mate_tr && !slot_due : 1'b1) &&
          (IQN - int'(cnt) + int'(pop)) >= (one ? 1 : 2);
    take = 2'd0;
    stop_after = 1'b0;
    if (can) begin
      if (slot_due) begin
        take = 2'd1;                                    // the slot, alone: the target follows the redirect
        stop_after = !early;
      end else if (tk0) begin
        if (nodelay(t0.op)) begin
          take = 2'd1; stop_after = 1'b1;
        end else if (same_word) begin
          take = 2'd2; stop_after = 1'b1;               // branch + slot
        end else begin
          take = 2'd1;                                  // slot is in the next word
        end
      end else if (same_word) begin
        take = 2'd2;
        if (tk1 && nodelay(t1.op)) stop_after = 1'b1;
      end else begin
        take = 2'd1;
      end
    end
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      cnt <= '0; rdy <= '0; tgt <= '0;
      stopped <= 1'b0; slot_due <= 1'b0; early <= 1'b0; after_rd <= 1'b0;
    end else begin
      logic [CW-1:0] c;
      logic [IQN-1:0] nrdy, ntgt;
      c = cnt - CW'(pop);
      nrdy = rdy; ntgt = tgt;
      for (int k = 0; k < IQN; k++) if (rf_mark[k]) nrdy[k] = 1'b1;
      if (refetch) begin
        nrdy = '0;
        ntgt = '0;
      end
      for (int k = 0; k < IQN; k++) begin
        int s; s = k + int'(pop);
        if (s < IQN) begin iq[k] <= iq[s]; rdy[k] <= nrdy[s]; tgt[k] <= ntgt[s]; end
        else begin rdy[k] <= 1'b0; tgt[k] <= 1'b0; end
      end
      if (take >= 1) begin iq[c] <= t0; rdy[c] <= 1'b1; tgt[c] <= after_rd; end
      if (take == 2) begin iq[c + 1] <= t1; rdy[c + 1] <= 1'b1; tgt[c + 1] <= 1'b0; end
      cnt <= c + CW'(take);
      // the next word fetched after the redirect (and after the slot, when the redirect came first) is the target
      if (take != 0) after_rd <= 1'b0;
      if (take != 0 && slot_due && early) after_rd <= 1'b1;
      // branch bookkeeping
      if (take != 0) begin
        if (slot_due) slot_due <= 1'b0;
        else if (tk0 && !nodelay(t0.op) && !same_word) slot_due <= 1'b1;
        else if (take == 2 && tk1 && !nodelay(t1.op)) slot_due <= 1'b1;
      end
      if (stop_after && !redirect) stopped <= 1'b1;
      if (stop_after || (take != 0 && slot_due)) early <= 1'b0;
      if (redirect) begin
        if (stopped || stop_after) after_rd <= 1'b1;
        if (stopped) stopped <= 1'b0;
        else if (!stop_after) early <= 1'b1;
      end
    end
  end

  assign q0_v = cnt >= 1 && rdy[0];
  assign q1_v = cnt >= 2 && rdy[0] && rdy[1];
  assign q0 = iq[0];
  assign q1 = iq[1];
  assign q0_tgt = tgt[0];
  assign q1_tgt = tgt[1];
endmodule
