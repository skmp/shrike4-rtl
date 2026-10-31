//
// v1_rstc_timing - standalone synthesis / timing harness around v1_rstc (rtl/v1/v1_rstc.sv): the reset controller --
// the RESET# pin, the WDT's power-on / manual resets, the H-UDI reset, the TLB multiple-hit resets, the CPU's reset
// request, and the resulting reset vector (rst) and reset code.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_rstc && quartus_sh --flow compile v1_rstc
// Fmax:  output_files/v1_rstc.sta.rpt
//
module v1_rstc_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: 0 reset_n, 1 sck2_i, 2 wdt_rst_por, 3 wdt_rst_man, 4 hudi_rst, 5 mhit_req, 6 arr_mhit,
    // 7 cpu_rst_req
    localparam integer IN_W = 8;
    localparam integer NREG = (IN_W + 31) / 32;
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

    // ---- DUT
    rst_t      d_rst;
    rst_code_e d_rst_code;
    v1_rstc u_dut (
        .clk(clk), .reset_n(in_flat[0]), .sck2_i(in_flat[1]), .wdt_rst_por(in_flat[2]), .wdt_rst_man(in_flat[3]),
        .hudi_rst(in_flat[4]), .mhit_req(in_flat[5]), .arr_mhit(in_flat[6]), .cpu_rst_req(in_flat[7]), .rst(d_rst),
        .rst_code(d_rst_code)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    rst_t      cap_rst;
    rst_code_e cap_rst_code;
    always @(posedge clk) begin
        if (reset) begin cap_rst <= '0; cap_rst_code <= rst_code_e'(0); end
        else       begin cap_rst <= d_rst; cap_rst_code <= d_rst_code; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_rst, cap_rst_code};
    end
endmodule
