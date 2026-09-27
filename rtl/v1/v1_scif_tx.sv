// v1_scif_tx.sv -- the asynchronous transmit sequencer of a serial channel (spec serial 3.2.2 / 3.2.4 / 3.2.5; SCI
// async 3.7 reuses it).  Owner: WI-SER.
//
// States IDLE / PRE / FRAME, stepped on base ticks (`tick`, from v1_scif_brg).  `pos` counts base ticks: in PRE since
// B0, in FRAME since S (the start bit's first tick).  The frame is start (0), 7 or 8 data bits LSB first, the parity
// bit (PE) or the multiprocessor bit (MP, SCI only), then 1 or 2 stop bits (1); L = 1 + data + (PE | MP) + 1 + STOP
// cells of 16 ticks.  The line idles at mark.
//
//   IDLE : at a tick with TE, `avail` (data queued) and not `block` (MCE / CTS) -> PRE.  That tick is B0.
//   PRE  : 6 ticks [HW run 2 E1: the fit pop B0 + 23 = S + 17, push B0 + 159, TEND B0 + 166 pins S = B0 + 6; why 6 is
//          not known, serial 9 Q1].  At B0 + 6 = S: TxD <- 0 (start), the format (CHR PE O/E STOP MP) is latched and
//          the parity accumulator starts at O/E.  If the data went away meanwhile (xFRST) -> IDLE [P].
//   S+16 : `load`: the head byte is copied into the shift register and D0 is driven.  The owner pops its FIFO on the
//          next tick (SCIF: the count and TDFE move at S + 17 = B0 + 23 [HW E1 K = 23]) or sets TDRE (SCI [P]).
//   S+16k: the next cell: data bit k-1 (XORed into the parity), then parity / MPB, then stop.
//   S+16L: `done` (the byte left the shift register).  With TE, `avail` and not `block` the next start bit begins on
//          this same tick (back to back, 16L ticks apart [HW E3: 160 ticks for 8N1]); else IDLE, and the owner's TEND
//          set condition becomes true [HW E1/E2: TEND = B0 + 6 + 16L, e.g. 166 for 8N1, 182 for 8N2 / 8E1 / 7E2].
//   TE=0 : the transmitter is initialised at once: IDLE, TxD at mark; the byte in the shift register is lost
//          [HW run 2 E6: 0x72 cut in D3 arrived as 0xFA, the FIFO kept 0x73].
// `idle_nx` is the state after this cycle (the owner's TEND level is evaluated on the next state, like the counts).
module v1_scif_tx (
  input  logic       clk,
  input  logic       rst,
  input  logic       en,
  input  logic       tick,       // base tick (implies en)
  input  logic       te,         // SCSCR.TE after this cycle's register access
  input  logic       avail,      // a byte is waiting (SCIF: T > 0 and not TFRST; SCI: TDRE = 0)
  input  logic       block,      // CTS forbids a frame start (SCIF MCE)
  input  logic [7:0] data,       // the byte to send (FIFO head / SCTDR1), taken at `load`
  input  logic       chr, pe, oe, stop, mp, mpbt,
  output logic       start,      // S: a start bit begins this tick
  output logic       load,       // S + 16: data copied into the shift register
  output logic       done,       // S + 16L: the frame ended
  output logic       idle_nx,
  output logic       txd,        // the serial output (registered)
  output logic [7:0] sh_o        // the byte of the frame in flight as sent (7-bit format: bit 7 = 0, it is not sent)
);
  localparam logic [1:0] S_IDLE = 2'd0, S_PRE = 2'd1, S_FRM = 2'd2;
  logic [1:0] st, st_n;
  logic [7:0] pos, pos_n, sh, sh_n, flen;
  logic       txd_n, par, par_n;
  logic       f_chr, f_pe, f_oe, f_stop, f_mp;
  logic       f_chr_n, f_pe_n, f_oe_n, f_stop_n, f_mp_n;
  logic [3:0] nd, k;

  assign sh_o = f_chr ? {1'b0, sh[6:0]} : sh;                        // the SCIF tap = what the line carried

  always_comb begin
    st_n = st; pos_n = pos; sh_n = sh; txd_n = txd; par_n = par;
    {f_chr_n, f_pe_n, f_oe_n, f_stop_n, f_mp_n} = {f_chr, f_pe, f_oe, f_stop, f_mp};
    start = 1'b0; load = 1'b0; done = 1'b0; k = 4'd0;
    nd   = f_chr ? 4'd7 : 4'd8;
    flen = {4'd2 + nd + {3'd0, f_pe | f_mp} + {3'd0, f_stop}, 4'd0};      // 16 * L
    if (!te) begin
      st_n = S_IDLE; txd_n = 1'b1; pos_n = '0;
    end else if (tick) begin
      unique case (st)
        S_IDLE: if (avail && !block) begin st_n = S_PRE; pos_n = '0; end
        S_PRE: begin
          if (pos == 8'd5) begin
            if (avail) begin
              st_n = S_FRM; pos_n = '0; txd_n = 1'b0; start = 1'b1; par_n = oe;
              {f_chr_n, f_pe_n, f_oe_n, f_stop_n, f_mp_n} = {chr, pe, oe, stop, mp};
            end else st_n = S_IDLE;
          end else pos_n = pos + 8'd1;
        end
        default: begin                                                  // S_FRM
          if (pos + 8'd1 == flen) begin
            done = 1'b1;
            if (avail && !block) begin
              pos_n = '0; txd_n = 1'b0; start = 1'b1; par_n = oe;
              {f_chr_n, f_pe_n, f_oe_n, f_stop_n, f_mp_n} = {chr, pe, oe, stop, mp};
            end else begin
              st_n = S_IDLE; txd_n = 1'b1; pos_n = '0;
            end
          end else begin
            pos_n = pos + 8'd1;
            if (pos_n[3:0] == 4'd0) begin
              k = pos_n[7:4];
              if (k == 4'd1) begin
                load = 1'b1; sh_n = data; txd_n = data[0]; par_n = par ^ data[0];
              end else if (k <= nd) begin
                txd_n = sh[3'(k - 4'd1)]; par_n = par ^ sh[3'(k - 4'd1)];
              end else if (k == nd + 4'd1 && (f_pe || f_mp)) begin
                txd_n = f_mp ? mpbt : par;                                // MPBT is not double-buffered: live
              end else txd_n = 1'b1;                                      // stop bit(s)
            end
          end
        end
      endcase
    end
    idle_nx = st_n == S_IDLE;
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      st <= S_IDLE; pos <= '0; sh <= '0; txd <= 1'b1; par <= 1'b0;
      {f_chr, f_pe, f_oe, f_stop, f_mp} <= '0;
    end else if (en) begin
      st <= st_n; pos <= pos_n; sh <= sh_n; txd <= txd_n; par <= par_n;
      {f_chr, f_pe, f_oe, f_stop, f_mp} <= {f_chr_n, f_pe_n, f_oe_n, f_stop_n, f_mp_n};
    end
  end
endmodule
