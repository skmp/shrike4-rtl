// v1_dmac_tb_top.sv -- v1_dmac with every struct port as scalar ports for tb/v1_dmac_tb.cpp (the BSC stub, the P4
// bridge stub, the CPU register driver, the DDT / DREQ drivers and the reference model live in C++), plus read-only
// taps into the DMAC's internal unit events for the lockstep comparison with tb/dmac_ref.h.  `make v1-dmac`.
module v1_dmac_tb_top import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst_pin, rst_por, rst_man, rst_cpu,
  input  logic        bce, pce, run, stby, mstp,
  // the bridge's strobe of the DMAC register slave (P4B_DMAC)
  input  logic        pq_rd, pq_wr,
  input  logic [1:0]  pq_size,
  input  logic [27:0] pq_off,
  input  logic [31:0] pq_wdata,
  input  logic        pq_dma,
  output logic [31:0] pr_rdata,
  output logic        pr_hit, pr_stall,
  // the DMAC as bridge master 1
  output logic        p4q_v, p4q_we,
  output logic [1:0]  p4q_size,
  output logic [31:0] p4q_addr, p4q_wdata,
  output logic        p4q_dma,
  input  logic        p4r_ack,
  input  logic [31:0] p4r_rdata,
  input  logic        p4r_err,
  // the DMAC's BSC port
  output logic        bq_v, bq_we,
  output logic [28:0] bq_addr,
  output logic [2:0]  bq_size,
  output logic        bq_lock,
  output logic [1:0]  bq_master, bq_ch,
  input  logic        bs_ready, bs_wd_req,
  input  logic [1:0]  bs_wd_beat,
  input  logic        bs_rd_v,
  input  logic [1:0]  bs_rd_beat,
  input  logic [63:0] bs_rd_data,
  input  logic        bs_rd_last, bs_done,
  output logic [63:0] bwd_data,
  output logic [7:0]  bwd_be,
  // requests, pins, interrupts
  input  logic [4:0]  dreq_i,                  // {sci_tx, sci_rx, scif_tx, scif_rx, tmu_ic}
  output logic        dack_ic,
  input  logic        dbreq_n, tr_n,
  output logic        bavl_n, tdack_n,
  output logic [1:0]  ddt_id,
  input  logic        tl_dtr_v,
  input  logic [63:0] tl_dtr,
  input  logic        tl_ch2_req, tl_tr_repeat,
  input  logic [63:0] ddt_d,                   // D63:0: the DTR in the DTR cycle (M6, board phase 2)
  output logic        bus_rel,                 // (M6) the engine holds no bus session
  output logic        bus_ses,                 // (M6-fix) a burst session holds the bus
  output logic        ddt_bus_req,
  input  logic        ddt_bavl,
  input  logic        nmi_evt,
  output logic [3:0]  dmte,
  output logic        dmae,
  // taps
  output logic        t_acc_v,
  output logic [1:0]  t_acc_ch,
  output logic [3:0]  t_cand,
  output logic        t_upd_v,
  output logic [1:0]  t_upd_ch,
  output logic        t_rr_idle,
  output logic        t_idle,                  // the engine is in its IDLE state (an arbitration Bclk when bce)
  output logic        t_busy,
  output logic        t_flag0, t_srv0,         // the DDT channel-0 flag and its in-service state
  output logic [3:0]  t_has,
  output logic [3:0]  t_en, t_hold, t_pop,
  output logic [8:0]  t_qcnt,
  output logic [31:0] t_sar0, t_sar1, t_sar2, t_sar3, t_dar0, t_dar1, t_dar2, t_dar3,
  output logic [31:0] t_chcr0, t_chcr1, t_chcr2, t_chcr3,
  output logic [23:0] t_tcr0, t_tcr1, t_tcr2, t_tcr3,
  output logic [31:0] t_dmaor
);
  rst_t     rst;
  clken_t   ce;
  p4s_req_t pq;
  p4s_rsp_t pr;
  p4m_req_t p4q;
  p4m_rsp_t p4r;
  bq_t      bq;
  bs_t      bs;
  bwd_t     bwd;
  dreq_t    dreq;
  ddt_tl_t  ddt_tl;

  assign rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
  assign ce  = '{bce: bce, pce: pce};
  always_comb begin
    pq = '0;
    pq.rd = pq_rd; pq.wr = pq_wr; pq.size = pq_size; pq.blk = P4B_DMAC; pq.off = pq_off; pq.wdata = pq_wdata;
    pq.dma = pq_dma;
  end
  assign pr_rdata = pr.rdata;
  assign pr_hit   = pr.hit;
  assign pr_stall = pr.stall;
  assign p4q_v = p4q.v;  assign p4q_we = p4q.we;  assign p4q_size = p4q.size;  assign p4q_addr = p4q.addr;
  assign p4q_wdata = p4q.wdata;  assign p4q_dma = p4q.dma;
  assign p4r = '{ack: p4r_ack, rdata: p4r_rdata, err: p4r_err};
  assign bq_v = bq.v;  assign bq_we = bq.r.we;  assign bq_addr = bq.r.addr;  assign bq_size = 3'(bq.r.size);
  assign bq_lock = bq.r.lock;  assign bq_master = 2'(bq.r.master);  assign bq_ch = bq.r.ch;
  assign bs = '{ready: bs_ready, wd_req: bs_wd_req, wd_beat: bs_wd_beat, rd_v: bs_rd_v, rd_beat: bs_rd_beat,
                rd_data: bs_rd_data, rd_last: bs_rd_last, done: bs_done};
  assign bwd_data = bwd.wd_data;  assign bwd_be = bwd.wd_be;
  assign dreq = '{sci_tx: dreq_i[4], sci_rx: dreq_i[3], scif_tx: dreq_i[2], scif_rx: dreq_i[1], tmu_ic: dreq_i[0]};
  assign ddt_tl = '{dtr_v: tl_dtr_v, dtr: tl_dtr, ch2_req: tl_ch2_req, tr_repeat: tl_tr_repeat};

  v1_dmac u_dut (
    .clk, .rst, .ce, .run, .stby, .mstp, .pq, .pr, .p4q, .p4r, .bq, .bs, .bwd, .dreq, .dack_ic,
    .dbreq_n, .tr_n, .bavl_n, .tdack_n, .ddt_id, .ddt_tl, .ddt_d, .ddt_bus_req, .ddt_bavl, .bus_rel, .bus_ses, .nmi_evt,
    .dmte, .dmae
  );

  // taps (read only)
  assign t_acc_v   = u_dut.acc_v;
  assign t_acc_ch  = u_dut.acc_ch;
  assign t_cand    = u_dut.cand;
  assign t_upd_v   = u_dut.upd_v;
  assign t_upd_ch  = u_dut.upd_ch;
  assign t_rr_idle = u_dut.rr_idle;
  assign t_busy    = u_dut.busy;
  assign t_idle    = 3'(u_dut.u_xfer.st) == 3'd0;                    // ST_IDLE
  assign t_flag0   = u_dut.u_ddt.flag0;
  assign t_srv0    = u_dut.u_ddt.srv0;
  assign t_has     = u_dut.ddt_has;
  assign t_en      = u_dut.en;
  assign t_hold    = u_dut.u_req.hold;
  assign t_pop     = u_dut.ddt_pop;
  assign t_qcnt    = u_dut.ddt_qcnt;
  assign t_sar0 = u_dut.sar[0];  assign t_sar1 = u_dut.sar[1];  assign t_sar2 = u_dut.sar[2];  assign t_sar3 = u_dut.sar[3];
  assign t_dar0 = u_dut.dar[0];  assign t_dar1 = u_dut.dar[1];  assign t_dar2 = u_dut.dar[2];  assign t_dar3 = u_dut.dar[3];
  assign t_chcr0 = u_dut.chcr[0];  assign t_chcr1 = u_dut.chcr[1];  assign t_chcr2 = u_dut.chcr[2];
  assign t_chcr3 = u_dut.chcr[3];
  assign t_tcr0 = u_dut.tcr[0];  assign t_tcr1 = u_dut.tcr[1];  assign t_tcr2 = u_dut.tcr[2];  assign t_tcr3 = u_dut.tcr[3];
  assign t_dmaor = {16'h0, u_dut.ddt, 5'd0, u_dut.prm, 5'd0, u_dut.ae, u_dut.nmif, u_dut.dme};
endmodule
