//
// v1_fetch_timing - standalone synthesis / timing harness around v1_fetch (rtl/v1/v1_fetch.sv): the instruction fetch
// unit of v1_core (the fetch PC and its request to the I-cache, the instruction queue with its pre-decode (four
// tp_decode) and the word decode (two), redirects / refetch / exception entry, the UBC fetch-side tags), with IQN = 6
// as v1_core instantiates it.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_fetch && quartus_sh --flow compile v1_fetch
// Fmax:  output_files/v1_fetch.sta.rpt
//
module v1_fetch_timing import tp_pkg::*, v1_pkg::*, v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RPC  = 0;                       // reset_pc
    localparam integer O_TGT  = O_RPC + 32;              // target
    localparam integer O_SPC  = O_TGT + 32;              // slot_pc
    localparam integer O_RFPC = O_SPC + 32;              // rf_pc
    localparam integer O_EXT  = O_RFPC + 32;             // exc_target
    localparam integer O_IFR  = O_EXT + 32;              // ifr
    localparam integer O_UBT  = O_IFR + $bits(irsp_t);   // ubc_t
    localparam integer O_POP  = O_UBT + $bits(ubc_ift_t);  // pop
    localparam integer O_BITS = O_POP + 2;               // 0 rst, 1 hold, 2 hold_ma, 3 md, 4 redirect, 5 keep,
                                                         // 6 slot_pend, 7 refetch, 8 exc_redirect, 9 flush, 10 skip,
                                                         // 11 ubc_pre_armed
    localparam integer IN_W   = O_BITS + 12;
    localparam integer NREG   = (IN_W + 31) / 32;
    reg [31:0] in_reg [0:NREG-1];
    always @(posedge clk) begin
        if (reset) begin
            for (int i = 0; i < NREG; i++) in_reg[i] <= 32'd0;
        end else if (wr_en && wr_addr < NREG) begin
            in_reg[wr_addr] <= wr_data;
        end
    end
    reg [NREG*32-1:0] in_flat;
    always @* for (int i = 0; i < NREG; i++) in_flat[i*32 +: 32] = in_reg[i];

    irsp_t    q_ifr;
    ubc_ift_t q_ubc_t;
    always @* begin
        q_ifr   = irsp_t'(in_flat[O_IFR +: $bits(irsp_t)]);
        q_ubc_t = ubc_ift_t'(in_flat[O_UBT +: $bits(ubc_ift_t)]);
    end

    // ---- DUT
    ireq_t       d_ifq;
    ubc_ifq_t    d_ubc_q;
    logic        d_q0_v, d_q1_v, d_q2_v, d_q0_tgt, d_q1_tgt, d_q1_mem, d_q2_mem;
    logic [31:0] d_q0_pc, d_q1_pc, d_fpc;
    logic [15:0] d_q0_op, d_q1_op, d_w_op2, d_w_op3;
    logic [4:0]  d_q0_tag, d_q1_tag, d_q2_tag;
    fx_e         d_q0_fx, d_q1_fx;
    logic [3:0]  d_w_mem;
    logic [2:0]  d_w_br;
    logic        d_busy_wait, d_ev_fetch, d_ev_fetch_all;
    v1_fetch #(.IQN(6)) u_dut (
        .clk(clk), .rst(in_flat[O_BITS + 0]), .reset_pc(in_flat[O_RPC +: 32]),
        .hold(in_flat[O_BITS + 1]), .hold_ma(in_flat[O_BITS + 2]), .md(in_flat[O_BITS + 3]),
        .ifq(d_ifq), .ifr(q_ifr), .ubc_q(d_ubc_q), .ubc_t(q_ubc_t),
        .q0_v(d_q0_v), .q1_v(d_q1_v), .q2_v(d_q2_v), .q0_pc(d_q0_pc), .q1_pc(d_q1_pc), .q0_op(d_q0_op), .q1_op(d_q1_op),
        .q0_tgt(d_q0_tgt), .q1_tgt(d_q1_tgt), .q0_tag(d_q0_tag), .q1_tag(d_q1_tag), .q0_fx(d_q0_fx), .q1_fx(d_q1_fx),
        .q1_mem(d_q1_mem), .q2_mem(d_q2_mem), .w_mem(d_w_mem), .w_br(d_w_br), .w_op2(d_w_op2), .w_op3(d_w_op3),
        .q2_tag(d_q2_tag),
        .pop(in_flat[O_POP +: 2]), .redirect(in_flat[O_BITS + 4]), .keep(in_flat[O_BITS + 5]),
        .slot_pend(in_flat[O_BITS + 6]), .target(in_flat[O_TGT +: 32]), .slot_pc(in_flat[O_SPC +: 32]),
        .refetch(in_flat[O_BITS + 7]), .rf_pc(in_flat[O_RFPC +: 32]), .exc_redirect(in_flat[O_BITS + 8]),
        .exc_target(in_flat[O_EXT +: 32]), .flush(in_flat[O_BITS + 9]), .skip(in_flat[O_BITS + 10]),
        .ubc_pre_armed(in_flat[O_BITS + 11]), .fpc(d_fpc), .busy_wait(d_busy_wait), .ev_fetch(d_ev_fetch),
        .ev_fetch_all(d_ev_fetch_all)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    ireq_t     cap_ifq;
    ubc_ifq_t  cap_ubc_q;
    fx_e       cap_q0_fx, cap_q1_fx;
    reg [95:0] cap_pc;                                   // q0_pc, q1_pc, fpc
    reg [63:0] cap_op;                                   // q0_op, q1_op, w_op2, w_op3
    reg [31:0] cap_misc;
    always @(posedge clk) begin
        if (reset) begin
            cap_ifq <= '0; cap_ubc_q <= '0; cap_q0_fx <= fx_e'(0); cap_q1_fx <= fx_e'(0);
            cap_pc <= '0; cap_op <= '0; cap_misc <= '0;
        end else begin
            cap_ifq   <= d_ifq;
            cap_ubc_q <= d_ubc_q;
            cap_q0_fx <= d_q0_fx;
            cap_q1_fx <= d_q1_fx;
            cap_pc    <= {d_q0_pc, d_q1_pc, d_fpc};
            cap_op    <= {d_q0_op, d_q1_op, d_w_op2, d_w_op3};
            cap_misc  <= {d_q0_v, d_q1_v, d_q2_v, d_q0_tgt, d_q1_tgt, d_q1_mem, d_q2_mem, d_q0_tag, d_q1_tag, d_q2_tag,
                          d_w_mem, d_w_br, d_busy_wait, d_ev_fetch, d_ev_fetch_all};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_ifq, cap_ubc_q, cap_q0_fx, cap_q1_fx, cap_pc, cap_op, cap_misc};
    end
endmodule
