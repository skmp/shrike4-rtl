//
// v1_bsc_port_timing - standalone synthesis / timing harness around v1_bsc_port (rtl/v1/v1_bsc_port.sv): the BSC's
// general-purpose port -- PCTRA / PCTRB (direction and pull-up), PDTRA / PDTRB (data), GPIOIC (interrupt control), the
// port pins and the GPIO interrupt request.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_bsc_port && quartus_sh --flow compile v1_bsc_port
// Fmax:  output_files/v1_bsc_port.sta.rpt
//
module v1_bsc_port_timing import v1_soc_pkg::*; (
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
    localparam integer O_WDAT = O_CE + $bits(clken_t);           // wdat
    localparam integer O_PORT = O_WDAT + 32;                     // port_i
    localparam integer O_BITS = O_PORT + 20;                     // 0 wr_pctra, 1 wr_pdtra, 2 wr_pctrb, 3 wr_pdtrb, 4 wr_gpioic,
                                                                 // 5 porten
    localparam integer IN_W   = O_BITS + 6;
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

    rst_t   q_rst;
    clken_t q_ce;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
    end

    // ---- DUT
    logic [19:0] d_port_o, d_port_oe;
    logic [31:0] d_pctra, d_pctrb;
    logic [15:0] d_pdtra, d_pdtrb, d_gpioic;
    logic        d_gpio;
    v1_bsc_port u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .wr_pctra(in_flat[O_BITS + 0]), .wr_pdtra(in_flat[O_BITS + 1]),
        .wr_pctrb(in_flat[O_BITS + 2]), .wr_pdtrb(in_flat[O_BITS + 3]), .wr_gpioic(in_flat[O_BITS + 4]),
        .wdat(in_flat[O_WDAT +: 32]), .porten(in_flat[O_BITS + 5]), .port_i(in_flat[O_PORT +: 20]),
        .port_o(d_port_o), .port_oe(d_port_oe), .pctra(d_pctra), .pctrb(d_pctrb), .pdtra(d_pdtra), .pdtrb(d_pdtrb),
        .gpioic(d_gpioic), .gpio(d_gpio)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [152:0] cap;                                     // port_o, port_oe, pctra, pctrb, pdtra, pdtrb, gpioic, gpio
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_port_o, d_port_oe, d_pctra, d_pctrb, d_pdtra, d_pdtrb, d_gpioic, d_gpio};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
