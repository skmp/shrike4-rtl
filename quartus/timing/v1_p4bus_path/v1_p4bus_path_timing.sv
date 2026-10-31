//
// v1_p4bus_path_timing - standalone synthesis / timing harness around v1_p4bus_path (rtl/v1/v1_p4bus.sv): one clock
// path of the P4 bridge (its request queue, the landing of a posted write, the slave strobe / stall / capture / ack
// pipeline). v1_p4bus instantiates three, with different parameters; this harness holds all three side by side, each
// fed from its own part of the input bank:
//   u_iclk  D1 0, D2 2  (I_D2), WREL 1, not posted                       -- the Iclk registers
//   u_bclk  D1 1, D2 6  (B_D2), WREL 2, posted                           -- the Bclk registers (BSC)
//   u_pclk  D1 1, D2 11 (P_D2), WREL 2, posted, 16-bit halves, landing   -- the Pclk peripherals
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_p4bus_path && quartus_sh --flow compile v1_p4bus_path
// Fmax:  output_files/v1_p4bus_path.sta.rpt
//
module v1_p4bus_path_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: one block per instance (offsets within a block below)
    localparam integer W_Q    = $bits(p4s_req_t);
    localparam integer B_INQ  = 0;                       // in_q
    localparam integer B_RD   = B_INQ + W_Q;             // st_rdata
    localparam integer B_MACK = B_RD + 32;               // mack
    localparam integer B_BITS = B_MACK + 2;              // 0 cen, 1 r_por, 2 r_hard, 3 r_cpu, 4 in_v, 5 in_m, 6 st_stall
    localparam integer BLK    = B_BITS + 7;
    localparam integer IN_W   = 3 * BLK;
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

    p4s_req_t q_in_q [3];
    always @* for (int k = 0; k < 3; k++) q_in_q[k] = p4s_req_t'(in_flat[k * BLK + B_INQ +: W_Q]);

    // ---- DUTs (outputs per instance k)
    logic        d_idle [3], d_acc [3], d_pw_acc [3], d_st_v [3], d_sl_v [3], d_cap [3], d_ack [3], d_ack_m [3];
    logic        d_slot_pw [3], d_wpend [3];
    p4s_req_t    d_st_q [3], d_sl_q [3], d_ack_q [3], d_slot_q [3];
    logic [31:0] d_ack_rdata [3];
    logic [1:0]  d_own [3];
    v1_p4bus_path #(.D1(0), .D2(2), .WREL(1), .POSTED(1'b0), .HALF16(1'b0)) u_iclk (
        .clk(clk), .cen(in_flat[0 * BLK + B_BITS + 0]), .r_por(in_flat[0 * BLK + B_BITS + 1]),
        .r_hard(in_flat[0 * BLK + B_BITS + 2]), .r_cpu(in_flat[0 * BLK + B_BITS + 3]), .mack(in_flat[0 * BLK + B_MACK +: 2]),
        .in_v(in_flat[0 * BLK + B_BITS + 4]), .in_m(in_flat[0 * BLK + B_BITS + 5]), .in_q(q_in_q[0]),
        .idle(d_idle[0]), .acc(d_acc[0]), .pw_acc(d_pw_acc[0]), .st_v(d_st_v[0]), .st_q(d_st_q[0]), .sl_v(d_sl_v[0]),
        .sl_q(d_sl_q[0]), .st_rdata(in_flat[0 * BLK + B_RD +: 32]), .st_stall(in_flat[0 * BLK + B_BITS + 6]),
        .cap(d_cap[0]), .ack(d_ack[0]), .ack_m(d_ack_m[0]), .ack_rdata(d_ack_rdata[0]), .ack_q(d_ack_q[0]),
        .own(d_own[0]), .slot_pw(d_slot_pw[0]), .slot_q(d_slot_q[0]), .wpend(d_wpend[0])
    );
    v1_p4bus_path #(.D1(1), .D2(6), .WREL(2), .POSTED(1'b1), .HALF16(1'b0)) u_bclk (
        .clk(clk), .cen(in_flat[1 * BLK + B_BITS + 0]), .r_por(in_flat[1 * BLK + B_BITS + 1]),
        .r_hard(in_flat[1 * BLK + B_BITS + 2]), .r_cpu(in_flat[1 * BLK + B_BITS + 3]), .mack(in_flat[1 * BLK + B_MACK +: 2]),
        .in_v(in_flat[1 * BLK + B_BITS + 4]), .in_m(in_flat[1 * BLK + B_BITS + 5]), .in_q(q_in_q[1]),
        .idle(d_idle[1]), .acc(d_acc[1]), .pw_acc(d_pw_acc[1]), .st_v(d_st_v[1]), .st_q(d_st_q[1]), .sl_v(d_sl_v[1]),
        .sl_q(d_sl_q[1]), .st_rdata(in_flat[1 * BLK + B_RD +: 32]), .st_stall(in_flat[1 * BLK + B_BITS + 6]),
        .cap(d_cap[1]), .ack(d_ack[1]), .ack_m(d_ack_m[1]), .ack_rdata(d_ack_rdata[1]), .ack_q(d_ack_q[1]),
        .own(d_own[1]), .slot_pw(d_slot_pw[1]), .slot_q(d_slot_q[1]), .wpend(d_wpend[1])
    );
    v1_p4bus_path #(.D1(1), .D2(11), .WREL(2), .POSTED(1'b1), .HALF16(1'b1), .LAND(1'b1)) u_pclk (
        .clk(clk), .cen(in_flat[2 * BLK + B_BITS + 0]), .r_por(in_flat[2 * BLK + B_BITS + 1]),
        .r_hard(in_flat[2 * BLK + B_BITS + 2]), .r_cpu(in_flat[2 * BLK + B_BITS + 3]), .mack(in_flat[2 * BLK + B_MACK +: 2]),
        .in_v(in_flat[2 * BLK + B_BITS + 4]), .in_m(in_flat[2 * BLK + B_BITS + 5]), .in_q(q_in_q[2]),
        .idle(d_idle[2]), .acc(d_acc[2]), .pw_acc(d_pw_acc[2]), .st_v(d_st_v[2]), .st_q(d_st_q[2]), .sl_v(d_sl_v[2]),
        .sl_q(d_sl_q[2]), .st_rdata(in_flat[2 * BLK + B_RD +: 32]), .st_stall(in_flat[2 * BLK + B_BITS + 6]),
        .cap(d_cap[2]), .ack(d_ack[2]), .ack_m(d_ack_m[2]), .ack_rdata(d_ack_rdata[2]), .ack_q(d_ack_q[2]),
        .own(d_own[2]), .slot_pw(d_slot_pw[2]), .slot_q(d_slot_q[2]), .wpend(d_wpend[2])
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_req_t  cap_q [3][4];                             // st_q, sl_q, ack_q, slot_q
    reg [31:0] cap_rdata [3];
    reg [11:0] cap_misc [3];
    always @(posedge clk) begin
        for (int k = 0; k < 3; k++) begin
            if (reset) begin
                for (int j = 0; j < 4; j++) cap_q[k][j] <= '0;
                cap_rdata[k] <= '0; cap_misc[k] <= '0;
            end else begin
                cap_q[k][0] <= d_st_q[k]; cap_q[k][1] <= d_sl_q[k]; cap_q[k][2] <= d_ack_q[k]; cap_q[k][3] <= d_slot_q[k];
                cap_rdata[k] <= d_ack_rdata[k];
                cap_misc[k]  <= {d_idle[k], d_acc[k], d_pw_acc[k], d_st_v[k], d_sl_v[k], d_cap[k], d_ack[k], d_ack_m[k],
                                 d_slot_pw[k], d_wpend[k], d_own[k]};
            end
        end
    end
    reg fold;
    always @* begin
        fold = 1'b0;
        for (int k = 0; k < 3; k++) begin
            for (int j = 0; j < 4; j++) fold = fold ^ (^cap_q[k][j]);
            fold = fold ^ (^cap_rdata[k]) ^ (^cap_misc[k]);
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= fold;
    end
endmodule
