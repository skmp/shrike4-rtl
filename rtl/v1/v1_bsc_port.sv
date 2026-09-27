// v1_bsc_port.sv -- the BSC's I/O ports: PCTRA PDTRA (port A, PA15-0 = D47-D32), PCTRB PDTRB (port B, PB19-16 =
// D51-D48), GPIOIC and the GPIO interrupt (rtl/v1/spec/bsc-memmap.md 3.3; hm 18).  Owner: WI-BSC.
//
//   * PCTRA / PCTRB: bit 2n = port n is an output, bit 2n+1 = its pull-up is off (pull-ups are not modelled: the board
//     drives port_i).  POR 0, held by a manual reset.  PCTRB keeps bits 7:0 only.
//   * PDTRA / PDTRB: a write stores the output latch (never reset; 0 at time 0 [P]); a read returns the latch for
//     output bits and the pin level for input bits [HW bsc-memmap 6.3: PDTRA 0x?0E7 with PCTRA 0x000A03F0].  Pins are
//     sampled on CKIO (one bce register) [M hm 18].  PDTRB keeps bits 3:0.
//   * The pins exist when BCR2.PORTEN = 1 (D51-D32 become the port); port_oe = 0 otherwise [P].
//   * GPIOIC: PTIRENn = 1 makes input pin n (0-15) an interrupt source; the request is the level-sensitive OR of the
//     enabled input pins that are low (INTEVT 0x620, IPRC[15:12]).  An enabled pin programmed as an output is ignored [P].
//   * Not modelled [P]: the "last MPX data" the console shows on undriven port inputs (bsc-memmap 6.3) -- the board sees
//     every beat and can drive port_i with it.
module v1_bsc_port import v1_soc_pkg::*; (
  input  logic        clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / man / cpu: only a power-on reset clears the control bits;
  input  rst_t        rst,                      // ce.pce: the SOC.md 3.8 port convention (pins sample on CKIO)
  input  clken_t      ce,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        wr_pctra, wr_pdtra, wr_pctrb, wr_pdtrb, wr_gpioic,
  input  logic [31:0] wdat,
  input  logic        porten,
  input  logic [19:0] port_i,
  output logic [19:0] port_o, port_oe,
  output logic [31:0] pctra, pctrb,
  output logic [15:0] pdtra, pdtrb, gpioic,
  output logic        gpio
);
  logic [31:0] ctra;
  logic [7:0]  ctrb;
  logic [15:0] dtra = '0;                        // never reset (hm 18.2)
  logic [3:0]  dtrb = '0;
  logic [15:0] gpic;
  logic [19:0] pin;                              // pin levels sampled on CKIO

  logic [15:0] ioa;
  logic [3:0]  iob;
  always_comb begin
    for (int n = 0; n < 16; n++) ioa[n] = ctra[2 * n];
    for (int n = 0; n < 4; n++)  iob[n] = ctrb[2 * n];
  end

  assign pctra   = ctra;
  assign pctrb   = {24'd0, ctrb};
  assign gpioic  = gpic;
  assign pdtra   = (dtra & ioa) | (pin[15:0] & ~ioa);
  assign pdtrb   = {12'd0, (dtrb & iob) | (pin[19:16] & ~iob)};
  assign port_o  = {dtrb, dtra};
  assign port_oe = porten ? {iob, ioa} : 20'd0;
  assign gpio    = |(gpic & ~ioa & ~pin[15:0]);

  always_ff @(posedge clk) begin
    if (ce.bce) pin <= port_i;
    if (rst.por) begin
      ctra <= '0; ctrb <= '0; gpic <= '0;
    end else begin
      if (wr_pctra)  ctra <= wdat;
      if (wr_pctrb)  ctrb <= wdat[7:0];
      if (wr_gpioic) gpic <= wdat[15:0];
    end
    if (wr_pdtra) dtra <= wdat[15:0];
    if (wr_pdtrb) dtrb <= wdat[3:0];
  end
endmodule
