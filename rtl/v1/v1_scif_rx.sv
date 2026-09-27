// v1_scif_rx.sv -- the asynchronous receive sequencer of a serial channel (spec serial 3.3.1, 3.3.5, 3.3.6; the SCI's
// async mode 3.7 reuses it with BRK_EN = 0).  Owner: WI-SER.
//
// States IDLE / FRAME / BREAK, stepped on base ticks.  `rxd` is the owner's input (SCIF: LOOP ? the transmitter's
// serial output : RxD through the 2-flop Pphi synchroniser).  `pos` counts base ticks since D.
//   IDLE  : with RE, the first tick at which the input is 0 is D (`start`; the owner restarts its DR timer).  The
//           format (CHR PE O/E MP) is latched at D [P serial 3.3.1 / 9 Q3].
//   D + 8 : the start bit is checked; a 1 there returns to IDLE [P serial 9 Q8].
//   D + 8 + 16k : cell k: data bit k-1 (LSB first), then the parity / MPB bit, then the (first) stop bit.  For 8N1 the
//           stop sample is D + 152; in loopback D = S + 1, so the push lands at B0 + 6 + 1 + 152 = B0 + 159 ticks
//           [HW run 2 E1 K = 159; E2: +16 per data or parity bit: 7N1 143, 8E1 175, 8N2 / 7E2 159].  Only the first
//           stop bit is checked; the receiver is back in IDLE after the stop sample and can see a new start bit on
//           the next tick (a 0 in a second stop-bit position starts the next frame).
//   stop sample : `done` with {data, fer = (stop == 0), per, mpb}; 7-bit data has bit 7 = 0 [P].
//   BREAK (BRK_EN) : after a stored-or-not frame with a framing error and data 0x00 (`brk_arm`), a next frame that is
//           0 in every sample (data 0x00, parity 0, stop 0: the line stayed at space for a frame) is not delivered:
//           `brk` instead, and the receiver waits in BREAK until an idle sample sees 1 (hm 16.2.7 BRK, serial 3.3.5;
//           manual only -- loopback cannot make a break).  Arming needs the framing-error frame's data to be 0x00, as
//           serial 3.3.5 says and as hm 16.1.1 ("the receive data following that in which a framing error occurred is
//           also at the space level") and the Figure 16.10 note ("the last data in SCFRDR2 is H'00, the break data in
//           which a framing error occurred") read; the BRK bit text of 16.2.7 names any framing-error frame.  The
//           two readings differ only for a break that starts inside a frame (e.g. 0x0F with FER, then space): here that
//           stores 0x0F(FER) and 0x00(FER) and sets BRK one frame later [P, unmeasurable on the DC; WI-SER-fix].
//   RE=0  : aborts the frame in progress (IDLE); flags, FIFO and counts are the owner's and are kept.
module v1_scif_rx #(
  parameter bit BRK_EN = 1'b1
) (
  input  logic       clk,
  input  logic       rst,
  input  logic       en,
  input  logic       tick,
  input  logic       re,
  input  logic       rxd,
  input  logic       chr, pe, oe, mp,
  output logic       start,      // D this tick
  output logic       done,       // stop sample: a frame is delivered
  output logic       brk,        // stop sample: a break frame (not delivered)
  output logic [7:0] data,
  output logic       fer, per, mpb,
  output logic       busy_nx     // in a frame after this cycle (the SCIF DR timer counts while not)
);
  localparam logic [1:0] S_IDLE = 2'd0, S_FRM = 2'd1, S_BRK = 2'd2;
  logic [1:0] st, st_n;
  logic [7:0] pos, pos_n, sh, sh_n;
  logic       par, par_n, pbit, pbit_n, arm, arm_n;
  logic       f_chr, f_pe, f_oe, f_mp, f_chr_n, f_pe_n, f_oe_n, f_mp_n;
  logic [3:0] nd, k;
  logic       np;

  always_comb begin
    st_n = st; pos_n = pos; sh_n = sh; par_n = par; pbit_n = pbit; arm_n = arm;
    {f_chr_n, f_pe_n, f_oe_n, f_mp_n} = {f_chr, f_pe, f_oe, f_mp};
    start = 1'b0; done = 1'b0; brk = 1'b0; k = 4'd0;
    nd   = f_chr ? 4'd7 : 4'd8;
    np   = f_pe | f_mp;
    data = f_chr ? {1'b0, sh[6:0]} : sh;
    fer  = !rxd;
    per  = f_pe && !f_mp && (par ^ pbit ^ f_oe);
    mpb  = f_mp && pbit;
    if (!re) begin
      st_n = S_IDLE; pos_n = '0; arm_n = 1'b0;
    end else if (tick) begin
      unique case (st)
        S_IDLE: begin
          if (!rxd) begin
            st_n = S_FRM; pos_n = '0; start = 1'b1; par_n = 1'b0; pbit_n = 1'b0;
            {f_chr_n, f_pe_n, f_oe_n, f_mp_n} = {chr, pe, oe, mp};
          end else arm_n = 1'b0;                                         // an idle 1: no break pending
        end
        S_FRM: begin
          pos_n = pos + 8'd1;
          if (pos_n == 8'd8) begin
            if (rxd) begin st_n = S_IDLE; pos_n = '0; end                // false start [P]
          end else if (pos_n[3:0] == 4'd8) begin
            k = pos_n[7:4];                                               // pos = 8 + 16k
            if (k <= nd) begin
              sh_n[3'(k - 4'd1)] = rxd; par_n = par ^ rxd;
            end else if (k == nd + 4'd1 && np) begin
              pbit_n = rxd;
            end else begin                                               // the (first) stop sample
              st_n = S_IDLE; pos_n = '0;
              if (BRK_EN && arm && !rxd && data == 8'd0 && !(np && pbit)) begin
                brk = 1'b1; st_n = S_BRK; arm_n = 1'b0;
              end else begin
                done = 1'b1; arm_n = BRK_EN && !rxd && data == 8'd0;
              end
            end
          end
        end
        default: if (rxd) st_n = S_IDLE;                                 // S_BRK: ends when an idle sample sees 1
      endcase
    end
    busy_nx = st_n == S_FRM;
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      st <= S_IDLE; pos <= '0; sh <= '0; par <= 1'b0; pbit <= 1'b0; arm <= 1'b0;
      {f_chr, f_pe, f_oe, f_mp} <= '0;
    end else if (en) begin
      st <= st_n; pos <= pos_n; sh <= sh_n; par <= par_n; pbit <= pbit_n; arm <= arm_n;
      {f_chr, f_pe, f_oe, f_mp} <= {f_chr_n, f_pe_n, f_oe_n, f_mp_n};
    end
  end
endmodule
