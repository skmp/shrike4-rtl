//
// v1_intc_timing - standalone synthesis / timing harness around v1_intc (rtl/v1/v1_intc.sv): the interrupt controller
// -- ICR / IPRA-IPRD on the P4 bus, the IRL pins (level encoded or independent), NMI with its edge select, the on-chip
// sources, the highest-priority pick against SR.IMASK / SR.BL, the accepted interrupt's INTEVT code, and the wake-up
// from sleep / standby.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_intc && quartus_sh --flow compile v1_intc
// Fmax:  output_files/v1_intc.sta.rpt
//
module v1_intc_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_CE   = O_RST + $bits(rst_t);
    localparam integer O_PQ   = O_CE + $bits(clken_t);           // pq
    localparam integer O_SRC  = O_PQ + $bits(p4s_req_t);         // src
    localparam integer O_IRL  = O_SRC + $bits(irq_src_t);        // irl_n
    localparam integer O_IMK  = O_IRL + 4;                       // sr_imask
    localparam integer O_BITS = O_IMK + 4;                       // 0 nmi_pin, 1 sr_bl, 2 asleep, 3 stby, 4 rtc_tick,
                                                                 // 5 irq_ack, 6 nmi_ack
    localparam integer IN_W   = O_BITS + 7;
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

    rst_t     q_rst;
    clken_t   q_ce;
    p4s_req_t q_pq;
    irq_src_t q_src;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_pq  = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
        q_src = irq_src_t'(in_flat[O_SRC +: $bits(irq_src_t)]);
    end

    // ---- DUT
    p4s_rsp_t d_pr;
    irq_t     d_irq;
    logic     d_wake, d_wake_stby, d_nmi_evt;
    v1_intc u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .pq(q_pq), .pr(d_pr), .irl_n(in_flat[O_IRL +: 4]),
        .nmi_pin(in_flat[O_BITS + 0]), .src(q_src), .sr_imask(in_flat[O_IMK +: 4]), .sr_bl(in_flat[O_BITS + 1]),
        .asleep(in_flat[O_BITS + 2]), .stby(in_flat[O_BITS + 3]), .rtc_tick(in_flat[O_BITS + 4]), .irq(d_irq),
        .irq_ack(in_flat[O_BITS + 5]), .nmi_ack(in_flat[O_BITS + 6]), .wake(d_wake), .wake_stby(d_wake_stby),
        .nmi_evt(d_nmi_evt)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr;
    irq_t     cap_irq;
    reg [2:0] cap_misc;                                  // wake, wake_stby, nmi_evt
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_irq <= '0; cap_misc <= '0; end
        else       begin cap_pr <= d_pr; cap_irq <= d_irq; cap_misc <= {d_wake, d_wake_stby, d_nmi_evt}; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_irq, cap_misc};
    end
endmodule
