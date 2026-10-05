// v1_bsc_tb.sv -- WI-BSC's unit-test top: v1_biu + v1_bsc with every struct port as flat ports for tb/v1_bsc_tb.cpp
// (the requester models, the board, the SDRAM / MPX protocol checkers and the register model live in C++).
module v1_bsc_tb import v1_soc_pkg::*, v1_bsc_pkg::*; (
  input  logic                   clk,
  input  logic                   rst_pin, rst_por, rst_man, rst_cpu,
  input  logic                   bce, pce,
  // the seven CPU-side requesters (v1_biu, index = BIU_*)
  input  logic [6:0]             rq_v, rq_we, rq_lock,
  input  logic [6:0][28:0]       rq_addr,
  input  logic [6:0][2:0]        rq_size,
  input  logic [6:0][63:0]       rwd_data,
  input  logic [6:0][7:0]        rwd_be,
  output logic [6:0]             rs_ready, rs_wd_req, rs_rd_v, rs_rd_last, rs_done,
  output logic [6:0][1:0]        rs_wd_beat, rs_rd_beat,
  output logic [6:0][63:0]       rs_rd_data,
  output logic                   cpu_bq_v,
  // the DMAC port
  input  logic                   dq_v, dq_we, dq_lock,
  input  logic [28:0]            dq_addr,
  input  logic [2:0]             dq_size,
  input  logic [1:0]             dq_master, dq_ch,
  input  logic [63:0]            dwd_data,
  input  logic [7:0]             dwd_be,
  output logic                   ds_ready, ds_wd_req, ds_rd_v, ds_rd_last, ds_done,
  output logic [1:0]             ds_wd_beat, ds_rd_beat,
  output logic [63:0]            ds_rd_data,
  // the P4 slave ports
  input  logic                   pq_rd, pq_wr, pq_sdmr,
  input  logic [1:0]             pq_size,
  input  logic [27:0]            pq_off,
  input  logic [31:0]            pq_wdata,
  output logic [31:0]            pr_rdata,
  output logic                   pr_hit, pr_stall,
  input  logic                   bpost_bsc,
  // the external bus
  output logic                   xb_start, xb_we, xb_lock, xb_dack, xb_beat, xb_last,
  output logic [2:0]             xb_area, xb_size,
  output logic [28:0]            xb_addr, xb_baddr,
  output logic [1:0]             xb_dack_id, xb_bidx,
  output logic [7:0]             xb_be,
  output logic [63:0]            xb_wdata,
  output logic                   xb_sd_cmd_v, xb_refresh, xb_mrs,
  output logic [3:0]             xb_sd_cmd,
  output logic [1:0]             xb_sd_bank,
  output logic [12:0]            xb_sd_row,
  output logic [11:0]            xb_mrs_val,
  input  logic                   xb_rdy,
  input  logic [63:0]            xb_rdata,
  input  logic                   breq_n, ddt_bus_req,
  output logic                   back_n, ddt_bavl,
  input  logic                   dq_rel,          // (M6) the DMAC holds no bus session (v1_dmac.bus_rel)
  input  logic                   dq_ses,          // (M6-fix) a DMAC burst session holds the bus (v1_dmac.bus_ses)
  input  logic [19:0]            port_i,
  output logic [19:0]            port_o, port_oe,
  input  logic [8:0]             md,
  output logic                   rcmi, rovi, gpio,
  output logic                   tap_v,
  output logic [31:0]            tap_addr,
  output logic [63:0]            tap_data,
  output logic [7:0]             tap_be,
  output logic                   bus_clash      // v1_bsc's simulation-only check: two sequencers on the bus in one Bcyc
);
  rst_t   rst;
  clken_t ce;
  assign rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
  assign ce  = '{bce: bce, pce: pce};

  bq_t  rq  [BIU_N];
  bs_t  rs  [BIU_N];
  bwd_t rwd [BIU_N];
  always_comb
    for (int i = 0; i < BIU_N; i++) begin
      rq[i]          = '0;
      rq[i].v        = rq_v[i];
      rq[i].r.we     = rq_we[i];
      rq[i].r.addr   = rq_addr[i];
      rq[i].r.size   = bsz_e'(rq_size[i]);
      rq[i].r.lock   = rq_lock[i];
      rq[i].r.master = BM_CPU;
      rwd[i]         = '{wd_data: rwd_data[i], wd_be: rwd_be[i]};
      rs_ready[i]    = rs[i].ready;
      rs_wd_req[i]   = rs[i].wd_req;
      rs_wd_beat[i]  = rs[i].wd_beat;
      rs_rd_v[i]     = rs[i].rd_v;
      rs_rd_beat[i]  = rs[i].rd_beat;
      rs_rd_data[i]  = rs[i].rd_data;
      rs_rd_last[i]  = rs[i].rd_last;
      rs_done[i]     = rs[i].done;
    end

  bq_t  cpu_bq, dma_bq;
  bs_t  cpu_bs, dma_bs;
  bwd_t cpu_bwd, dma_bwd;
  assign cpu_bq_v = cpu_bq.v;
  assign dma_bq   = '{v: dq_v, r: '{we: dq_we, addr: dq_addr, size: bsz_e'(dq_size), lock: dq_lock,
                                   master: bmaster_e'(dq_master), ch: dq_ch}};
  assign dma_bwd  = '{wd_data: dwd_data, wd_be: dwd_be};
  assign {ds_ready, ds_wd_req, ds_wd_beat, ds_rd_v, ds_rd_beat, ds_rd_data, ds_rd_last, ds_done} = dma_bs;

  p4s_req_t pq, pq_s;
  p4s_rsp_t pr, pr_s;
  always_comb begin
    pq = '0; pq_s = '0;
    if (pq_sdmr) begin
      pq_s.rd = pq_rd; pq_s.wr = pq_wr; pq_s.size = pq_size; pq_s.off = pq_off; pq_s.wdata = pq_wdata;
      pq_s.blk = pq_off[23:18] == 6'h25 ? P4B_SDMR3 : P4B_SDMR2;
    end else begin
      pq.rd = pq_rd; pq.wr = pq_wr; pq.size = pq_size; pq.off = pq_off; pq.wdata = pq_wdata; pq.blk = P4B_BSC;
    end
  end
  assign pr_rdata = pr.rdata | pr_s.rdata;
  assign pr_hit   = pr.hit | pr_s.hit;
  assign pr_stall = pr.stall | pr_s.stall;

  xb_out_t xb_o;
  xb_in_t  xb_i;
  assign xb_i = '{rdy: xb_rdy, rdata: xb_rdata};
  assign {xb_start, xb_area, xb_addr, xb_we, xb_size, xb_lock, xb_dack, xb_dack_id, xb_beat, xb_bidx, xb_baddr, xb_be,
          xb_wdata, xb_last, xb_sd_cmd_v, xb_sd_cmd, xb_sd_bank, xb_sd_row, xb_refresh, xb_mrs, xb_mrs_val} = xb_o;

  commit_rec_t [CT_BSC_N-1:0] tap;
  assign tap_v    = tap[0].v && tap[0].kind == CK_DMAW;
  assign tap_addr = tap[0].addr;
  assign tap_data = tap[0].data;
  assign tap_be   = tap[0].be;

  assign bus_clash = u_bsc.clash;

  v1_biu u_biu (.clk, .rst, .ce, .rq, .rs, .rwd, .cpu_bq, .cpu_bs, .cpu_bwd);
  v1_bsc u_bsc (.clk, .rst, .ce, .pq, .pr, .pq_sdmr(pq_s), .pr_sdmr(pr_s), .bpost_bsc, .cpu_bq, .cpu_bs, .cpu_bwd,
                .dma_bq, .dma_bs, .dma_bwd, .xb_o, .xb_i, .breq_n, .back_n, .ddt_bus_req, .ddt_bavl, .dma_rel(dq_rel),
                .dma_ses(dq_ses),
                .port_i, .port_o, .port_oe, .md, .rcmi, .rovi, .gpio, .tap);
endmodule
