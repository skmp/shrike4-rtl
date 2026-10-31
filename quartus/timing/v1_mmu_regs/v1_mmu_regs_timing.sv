//
// v1_mmu_regs_timing - standalone synthesis / timing harness around v1_mmu_regs (rtl/v1/v1_mmu_regs.sv): the MMU's
// registers (PTEH / PTEL / PTEA / TTB / TEA / MMUCR), their P4 access, the capture of a TLB exception's address, the
// URC step and the LRUI update.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_mmu_regs && quartus_sh --flow compile v1_mmu_regs
// Fmax:  output_files/v1_mmu_regs.sta.rpt
//
module v1_mmu_regs_timing import v1_soc_pkg::*, v1_mmu_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_PQ   = O_RST + $bits(rst_t);
    localparam integer O_EXCW = O_PQ + $bits(p4s_req_t);
    localparam integer O_CVA  = O_EXCW + $bits(excw_t);          // cap_va
    localparam integer O_LRUI = O_CVA + 32;                      // lrui_nx
    localparam integer O_BITS = O_LRUI + 6;                      // 0 cap_v, 1 urc_inc, 2 lrui_we
    localparam integer IN_W   = O_BITS + 3;
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
    p4s_req_t q_pq;
    excw_t    q_excw;
    always @* begin
        q_rst  = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_pq   = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
        q_excw = excw_t'(in_flat[O_EXCW +: $bits(excw_t)]);
    end

    // ---- DUT
    p4s_rsp_t    d_pr;
    logic [21:0] d_pteh_vpn;
    logic [7:0]  d_pteh_asid, d_asid_nx;
    logic [31:0] d_ptel;
    logic [3:0]  d_ptea;
    mmucr_t      d_mmucr;
    logic        d_ti, d_at_nx;
    v1_mmu_regs u_dut (
        .clk(clk), .rst(q_rst), .pq(q_pq), .pr(d_pr), .excw(q_excw), .cap_v(in_flat[O_BITS + 0]),
        .cap_va(in_flat[O_CVA +: 32]), .urc_inc(in_flat[O_BITS + 1]), .lrui_we(in_flat[O_BITS + 2]),
        .lrui_nx(in_flat[O_LRUI +: 6]), .pteh_vpn(d_pteh_vpn), .pteh_asid(d_pteh_asid), .ptel(d_ptel), .ptea(d_ptea),
        .mmucr(d_mmucr), .ti(d_ti), .at_nx(d_at_nx), .asid_nx(d_asid_nx)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr;
    mmucr_t    cap_mmucr;
    reg [75:0] cap_misc;                                 // pteh_vpn, pteh_asid, ptel, ptea, asid_nx, ti, at_nx
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_mmucr <= '0; cap_misc <= '0; end
        else begin
            cap_pr    <= d_pr;
            cap_mmucr <= d_mmucr;
            cap_misc  <= {d_pteh_vpn, d_pteh_asid, d_ptel, d_ptea, d_asid_nx, d_ti, d_at_nx};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_mmucr, cap_misc};
    end
endmodule
