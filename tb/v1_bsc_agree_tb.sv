// v1_bsc_agree_tb.sv -- WI-BSC: v1_biu + v1_bsc against tb/v1_bsc_stub.sv (rtl/v1/SOC.md 3.3.1: "WI-BSC's testbench
// checks that v1_bsc + v1_biu produce the same cycles as the stub for the stub's cases").  Verilator --binary --timing;
// `make v1-bsc-agree`.
//
// The scenarios are tb/v1_bsc_stub_tb.sv's, unchanged (C3 / C4 phases, C5 beats and wrap order, C6 / C7 penalties and
// stream rates, C8 drains / bursts / 64-bit stores, TAS, the SOC.md 3.3 order, the read overlap, manual reset, the
// handshake check).  Both models get the same requester inputs; the requester models react to the stub's strobes as
// in the stub's own testbench, and in every cycle the real pair's strobes (ready, wd_req / wd_beat, rd_v / rd_beat /
// rd_data / rd_last, done, all seven requesters) must equal the stub's.  The stub's own checks run too.
//
// The real BSC runs behind an SDRAM board model (area 3, every beat answered in its cycle, SOC.md 3.3.2) holding the
// same initial image as the stub's memory; at the end both memories must be equal.  Before the scenarios the BSC is
// programmed with the Dreamcast's registers over its P4 slave port (board.md 5.3; RTCSR CKS = 0: no refresh, the stub
// has none) and the test row is opened by one warm-up read (the stub has no rows: an SDRAM access to a closed bank
// pays the +6 of SOC.md C9).  Comparison starts after that.
//
// Cycle convention: `cyc` counts Icyc; ce.bce is high in the odd cycles (FRQCR 0x0E0A, the last Icyc of each Bcyc).
// A driver process wakes at a rising edge and reads the outputs of the cycle that just ended (cyc); it changes the
// inputs at the following falling edge (Verilator 5.014 runs a non-blocking assignment in an initial block as a
// blocking one).  Every check prints "FAIL: ..."; the run ends with "v1-bsc-agree tb: N checks, M failures".
/* verilator lint_off UNUSEDSIGNAL */          // each scenario checks some fields of the result records; memory index bits
module v1_bsc_agree_tb import v1_soc_pkg::*; import v1_bsc_stub_pkg::*; import v1_bsc_pkg::*; ;
  logic clk = 1'b0;
  always #1 clk <= ~clk;

  int     cyc = 0;
  always @(posedge clk) cyc <= cyc + 1;

  rst_t   rst;
  clken_t ce;
  assign ce.bce = cyc[0];
  assign ce.pce = cyc[1:0] == 2'd3;

  localparam int MEMQ = 4096;
  logic [63:0] mem [MEMQ];
  function automatic logic [63:0] pat(input int q);
    return {32'h8000_0000 | 32'(q * 8 + 4), 32'h4000_0000 | 32'(q * 8)};
  endfunction

  // the drivers write the packed copy: Verilator 5.014 does not re-evaluate combinational logic after a timed process
  // writes an element of an unpacked array, and C3's same-cycle ready is combinational from bq.v
  bq_t [BRQ_N-1:0] rq_d;
  bq_t   rq  [BRQ_N];
  always_comb for (int i = 0; i < BRQ_N; i++) rq[i] = rq_d[i];
  bs_t   rs  [BRQ_N];
  bwd_t  rwd [BRQ_N];
  logic        m_rd, m_wr, proto_err;
  logic [28:0] m_addr;
  logic [7:0]  m_be;
  logic [63:0] m_wdata, m_rdata;

  v1_bsc_stub u_stub (.clk, .rst, .ce, .rq, .rs, .rwd, .m_rd, .m_wr, .m_addr, .m_be, .m_wdata, .m_rdata, .proto_err);

  // ---- the real pair: v1_biu + v1_bsc, behind an SDRAM board model
  bs_t      rs_r [BRQ_N];
  bq_t      cpu_bq;
  bs_t      cpu_bs;
  bwd_t     cpu_bwd;
  p4s_req_t pq;
  p4s_rsp_t pr, pr_sdmr;
  xb_out_t  xb_o;
  xb_in_t   xb_i;
  logic     back_n, ddt_bavl, rcmi, rovi, gpio;
  logic [19:0] port_o, port_oe;
  commit_rec_t [CT_BSC_N-1:0] btap;
  bs_t      dma_bs;
  v1_biu u_biu (.clk, .rst, .ce, .rq, .rs(rs_r), .rwd, .cpu_bq, .cpu_bs, .cpu_bwd);
  v1_bsc u_bsc (.clk, .rst, .ce, .pq, .pr, .pq_sdmr('0), .pr_sdmr, .bpost_bsc(1'b0), .cpu_bq, .cpu_bs, .cpu_bwd,
                .dma_bq('0), .dma_bs, .dma_bwd('0), .xb_o, .xb_i, .breq_n(1'b1), .back_n, .ddt_bus_req(1'b0), .ddt_bavl, .dma_rel(1'b0),
                .dma_ses(1'b0),
                .port_i(20'h01CE3), .port_o, .port_oe, .md(9'h065), .rcmi, .rovi, .gpio, .tap(btap));

  logic [63:0] rmem [MEMQ];                      // the board's SDRAM (area 3), the stub memory's image
  logic        b_we;                             // the current bus cycle's direction (from xb.start)
  int          n_bad_bus = 0;
  initial for (int q = 0; q < MEMQ; q++) rmem[q] = pat(q);
  always_comb begin
    xb_i.rdy   = 1'b1;
    xb_i.rdata = rmem[xb_o.baddr[14:3]];
  end
  always @(posedge clk) begin
    if (xb_o.start) b_we <= xb_o.we;
    if (xb_o.beat && (xb_o.start ? xb_o.we : b_we))
      for (int b = 0; b < 8; b++) if (xb_o.be[b]) rmem[xb_o.baddr[14:3]][b*8 +: 8] <= xb_o.wdata[b*8 +: 8];
    if ((xb_o.start && xb_o.area != 3'd3) || ((xb_o.start || xb_o.beat) && !ce.bce)) n_bad_bus <= n_bad_bus + 1;
  end

  // ---- the stub's memory: 32 KB of quadwords, index = addr[14:3]; initial value: its own address in each longword
  assign m_rdata = mem[m_addr[14:3]];
  initial for (int q = 0; q < MEMQ; q++) mem[q] = pat(q);
  always @(posedge clk)
    if (m_wr) for (int b = 0; b < 8; b++) if (m_be[b]) mem[m_addr[14:3]][b*8 +: 8] <= m_wdata[b*8 +: 8];

  // ---- write data: the requester drives bwd in the Icyc after wd_req only; any other cycle carries poison
  logic [63:0] wq [BRQ_N][4];                  // per requester: data of quadword 0..3 of the line
  logic [7:0]  wbe [BRQ_N];
  always @(posedge clk)
    for (int i = 0; i < BRQ_N; i++)
      rwd[i] <= rs[i].wd_req ? bwd_t'{wd_data: wq[i][rs[i].wd_beat], wd_be: wbe[i]}
                             : bwd_t'{wd_data: 64'hDEAD_BEEF_DEAD_BEEF, wd_be: 8'hFF};

  // ---- checks
  int n_chk = 0, n_err = 0;
  task automatic chk(input bit ok, input string what);
    n_chk++;
    if (!ok) begin
      n_err++;
      if (n_err <= 40) $display("FAIL: %s", what);
    end
  endtask

  // every bs_t strobe is a one-Icyc pulse in a bce cycle (SOC.md 3.3.2); at most one memory beat per cycle
  initial forever begin
    @(posedge clk);
    if (!rst.por) begin
      for (int i = 0; i < BRQ_N; i++)
        if ((rs[i].ready || rs[i].wd_req || rs[i].rd_v || rs[i].rd_last || rs[i].done) && !ce.bce)
          chk(0, $sformatf("requester %0d: a strobe in the non-bce cycle %0d", i, cyc));
      if (m_rd && m_wr) chk(0, $sformatf("a read and a write beat in cycle %0d", cyc));
    end
  end

  // ---- the agreement: the real pair's strobes equal the stub's, every cycle, once `cmp` is on
  bit cmp = 1'b0;
  int n_cmp = 0, n_diff = 0;
  initial forever begin
    @(posedge clk);
    if (cmp && !rst.por) begin
      n_cmp++;
      for (int i = 0; i < BRQ_N; i++) begin
        bs_t a, b;
        a = rs[i]; b = rs_r[i];
        if (!a.rd_v) a.rd_data = '0;
        if (!b.rd_v) b.rd_data = '0;
        if (a != b) begin
          n_diff++;
          if (n_diff <= 30)
            $display("FAIL: cycle %0d requester %0d: stub ready %0d wd_req %0d/%0d rd_v %0d/%0d/%h last %0d done %0d | real ready %0d wd_req %0d/%0d rd_v %0d/%0d/%h last %0d done %0d",
                     cyc, i, a.ready, a.wd_req, a.wd_beat, a.rd_v, a.rd_beat, a.rd_data, a.rd_last, a.done,
                     b.ready, b.wd_req, b.wd_beat, b.rd_v, b.rd_beat, b.rd_data, b.rd_last, b.done);
        end
      end
      for (int i = 0; i < BRQ_N; i++)
        if ((rs_r[i].ready || rs_r[i].wd_req || rs_r[i].rd_v || rs_r[i].rd_last || rs_r[i].done) && !ce.bce)
          chk(0, $sformatf("real: requester %0d: a strobe in the non-bce cycle %0d", i, cyc));
    end
  end

  // ---- P4 register writes to the real BSC (Bclk strobes, one per bce cycle)
  task automatic p4w(input logic [27:0] off, input logic [1:0] sz, input logic [31:0] v);
    while (!(ce.bce == 1'b0)) @(posedge clk);          // start in a non-bce cycle so the strobe lands on a bce one
    @(negedge clk);
    pq = '0; pq.wr = 1'b1; pq.size = sz; pq.blk = P4B_BSC; pq.off = off; pq.wdata = v;
    @(posedge clk);
    chk(ce.bce, "p4w: the strobe was not in a bce cycle");
    @(negedge clk);
    pq = '0;
  endtask
  task automatic p4r(input logic [27:0] off, input logic [1:0] sz, output logic [31:0] v);
    while (!(ce.bce == 1'b0)) @(posedge clk);
    @(negedge clk);
    pq = '0; pq.rd = 1'b1; pq.size = sz; pq.blk = P4B_BSC; pq.off = off;
    @(posedge clk);
    v = pr.rdata;                                // combinational in the strobe cycle, read before the edge's updates
    @(negedge clk);
    pq = '0;
  endtask

  // ---- one transaction on requester i: bq.v rises in cycle t0 and is held through `ready`
  typedef struct packed {
    int                ready, done, nb, nwd;
    logic [3:0][31:0]  bc;                     // rd_v cycles
    logic [3:0][1:0]   bq;                     // rd_beat
    logic [3:0][63:0]  bd;                     // rd_data
    logic [3:0]        bl;                     // rd_last
    logic [3:0][31:0]  wc;                     // wd_req cycles
    logic [3:0][1:0]   wb;                     // wd_beat
  } res_t;

  function automatic breq_t mk(input logic we, input logic [28:0] a, input bsz_e sz, input logic lk);
    breq_t r;
    r = '0;
    r.we = we; r.addr = a; r.size = sz; r.lock = lk; r.master = BM_CPU;
    return r;
  endfunction

  // called and returning at a rising edge (the one that ends cycle `cyc`)
  task automatic xact(input int i, input int t0, input breq_t r, output res_t o);
    o.ready = -1; o.done = -1; o.nb = 0; o.nwd = 0;
    while (cyc < t0 - 1) @(posedge clk);
    chk(cyc == t0 - 1, $sformatf("requester %0d: start %0d requested at cycle %0d (scenario bug)", i, t0, cyc));
    @(negedge clk);
    rq_d[i] = '{v: 1'b1, r: r};
    forever begin
      logic drop;
      @(posedge clk);
      drop = 1'b0;
      if (rs[i].ready && o.ready < 0) begin o.ready = cyc; drop = 1'b1; end
      if (rs[i].wd_req && o.nwd < 4) begin o.wc[o.nwd] = cyc; o.wb[o.nwd] = rs[i].wd_beat; o.nwd++; end
      if (rs[i].rd_v && o.nb < 4) begin
        o.bc[o.nb] = cyc; o.bq[o.nb] = rs[i].rd_beat; o.bd[o.nb] = rs[i].rd_data; o.bl[o.nb] = rs[i].rd_last; o.nb++;
      end
      if (rs[i].done) begin o.done = cyc; break; end    // never in the ready cycle (done >= ready + 8)
      if (cyc > t0 + 300) begin chk(0, $sformatf("requester %0d: no done within 300 cycles of %0d", i, t0)); break; end
      if (drop) begin @(negedge clk); rq_d[i] = '0; end
    end
  endtask

  function automatic int odd_at(input int t, input bit odd);   // the first cycle >= t with the given parity
    return (t % 2 == int'(odd)) ? t : t + 1;
  endfunction
  task automatic idle(input int n); repeat (n) @(posedge clk); endtask
  function automatic int qi(input logic [28:0] a); return int'(a[14:3]); endfunction

  localparam logic [28:0] BASE = 29'h0C00_1000;   // a line-aligned test area

  // ============================================================================================================
  // C3 / C4: ready in the cycle bq.v rises if that is a bce cycle, else in the next (r = m + 1 / m + 2 by phase)
  task automatic s_c3c4();
    res_t o;
    breq_t reqs [4];
    int    port [4];
    reqs[0] = mk(0, BASE, BZ_LINE, 0);        port[0] = BRQ_OC_FILL;
    reqs[1] = mk(0, BASE + 4, BZ_L, 0);       port[1] = BRQ_OC_UNC;
    reqs[2] = mk(1, BASE + 12, BZ_L, 0);      port[2] = BRQ_OC_WTB;
    reqs[3] = mk(1, BASE + 32, BZ_LINE, 0);   port[3] = BRQ_SQ;
    for (int k = 0; k < 4; k++) begin
      wq[port[k]] = '{default: 64'h0123_4567_89AB_CDEF}; wbe[port[k]] = reqs[k].size == BZ_LINE ? 8'hFF : 8'hF0;
      for (int ph = 0; ph < 2; ph++) begin
        int t;
        t = odd_at(cyc + 3, ph[0]);
        xact(port[k], t, reqs[k], o);
        chk(o.ready == (ph == 1 ? t : t + 1),
            $sformatf("C3/C4 requester %0d: bq.v in %0d (%s) -> ready %0d, want %0d", port[k], t,
                      ph == 1 ? "bce" : "non-bce", o.ready, ph == 1 ? t : t + 1));
        idle(40);
      end
    end
  endtask

  // C5: a line read's beats at ready + 12 + 2k in wrap order from the critical quadword; B / W / L / Q reads
  task automatic s_c5();
    res_t o;
    for (int q = 0; q < 4; q++) begin
      logic [28:0] a;
      a = BASE + 29'(64 + q * 8);
      xact(BRQ_OC_FILL, odd_at(cyc + 2, q[0]), mk(0, a, BZ_LINE, 0), o);
      chk(o.nb == 4 && o.done == o.ready + 18, $sformatf("C5 line crit %0d: %0d beats, done at ready + %0d", q, o.nb,
                                                         o.done - o.ready));
      for (int k = 0; k < 4 && k < o.nb; k++) begin
        int qq;
        qq = (q + k) % 4;
        chk(o.bc[k] == o.ready + 12 + 2 * k, $sformatf("C5 line crit %0d beat %0d at ready + %0d, want %0d", q, k,
                                                       o.bc[k] - o.ready, 12 + 2 * k));
        chk(o.bq[k] == 2'(qq), $sformatf("C5 line crit %0d beat %0d: rd_beat %0d, want %0d", q, k, o.bq[k], qq));
        chk(o.bd[k] == pat(qi(a) - q + qq), $sformatf("C5 line crit %0d beat %0d: data %h", q, k, o.bd[k]));
        chk(o.bl[k] == (k == 3), $sformatf("C5 line crit %0d beat %0d: rd_last %0d", q, k, o.bl[k]));
      end
      idle(4);
    end
    // single transfers: the addressed lanes of the quadword, others 0
    for (int s = 0; s < 3; s++)
      for (int off = 0; off < 8; off += (1 << s)) begin
        logic [28:0] a;
        logic [7:0]  be;
        logic [63:0] m;
        a = BASE + 29'(128 + off);
        be = s == 0 ? 8'h01 << off : s == 1 ? 8'h03 << off : 8'h0F << off;
        for (int b = 0; b < 8; b++) m[b*8 +: 8] = {8{be[b]}};
        xact(BRQ_OC_UNC, cyc + 2, mk(0, a, bsz_e'(s), 0), o);
        chk(o.nb == 1 && o.bc[0] == o.ready + 12 && o.done == o.bc[0] && o.bl[0],
            $sformatf("C5 size %0d off %0d: %0d beats, rd_v at ready + %0d, done %0d", s, off, o.nb, o.bc[0] - o.ready,
                      o.done - o.ready));
        chk(o.bd[0] == (pat(qi(a)) & m) && o.bq[0] == a[4:3],
            $sformatf("C5 size %0d off %0d: data %h, want %h", s, off, o.bd[0], pat(qi(a)) & m));
        idle(2);
      end
    // Q: two longword cycles, one rd_v with the whole quadword at ready + 26
    xact(BRQ_OC_UNC, cyc + 2, mk(0, BASE + 136, BZ_Q, 0), o);
    chk(o.nb == 1 && o.bc[0] == o.ready + 26 && o.done == o.bc[0] && o.bd[0] == pat(qi(BASE + 136)),
        $sformatf("C5 Q read: %0d beats, rd_v at ready + %0d, data %h", o.nb, o.bc[0] - o.ready, o.bd[0]));
    idle(4);
  endtask

  // C6: load miss penalty ack - m = 14 / 15 by phase
  task automatic s_c6();
    res_t o;
    for (int ph = 0; ph < 2; ph++) begin
      int m;
      m = odd_at(cyc + 3, ph[0]);
      xact(BRQ_OC_FILL, m + 1, mk(0, BASE + 29'(192 + 16), BZ_LINE, 0), o);
      chk(o.bc[0] + 1 - m == (ph == 1 ? 15 : 14), $sformatf("C6 load miss with m %s: penalty %0d, want %0d",
          ph == 1 ? "on bce" : "off bce", o.bc[0] + 1 - m, ph == 1 ? 15 : 14));
      idle(20);
    end
  endtask

  // C7: uncached read penalty 14 / 15 by phase, 16 per access back to back; a 64-bit read 30 per access
  task automatic s_c7();
    res_t o;
    for (int ph = 0; ph < 2; ph++) begin
      int m, ack, prev;
      m = odd_at(cyc + 3, ph[0]);
      prev = -1;
      for (int k = 0; k < 6; k++) begin
        xact(BRQ_OC_UNC, m + 1, mk(0, BASE + 29'(256 + k * 4), BZ_L, 0), o);
        ack = o.bc[0] + 1;
        if (k == 0) chk(ack - m == (ph == 1 ? 15 : 14), $sformatf("C7 uncached read, m %s: penalty %0d",
                        ph == 1 ? "on bce" : "off bce", ack - m));
        else chk(ack - prev == 16, $sformatf("C7 uncached read stream: %0d per access, want 16", ack - prev));
        prev = ack;
        m = ack + 1;
      end
      idle(20);
    end
    begin
      int m, ack, prev;
      m = cyc + 3;
      prev = -1;
      for (int k = 0; k < 4; k++) begin
        xact(BRQ_OC_UNC, m + 1, mk(0, BASE + 29'(264 + k * 8), BZ_Q, 0), o);
        ack = o.bc[0] + 1;
        if (k > 0) chk(ack - prev == 30, $sformatf("C7 64-bit uncached read stream: %0d per access, want 30",
                                                    ack - prev));
        prev = ack;
        m = ack + 1;
      end
      idle(20);
    end
  endtask

  // C8: posted writes.  Store stream through the WTB 10 per store; SQ bursts 12 per 32 bytes; 64-bit stores 20
  task automatic s_c8();
    res_t o;
    int   m, x, prev_done, prev_rdy, prev_ack, ack;
    // B / W / L stores through the WTB, back to back
    m = cyc + 3; prev_done = -100; prev_rdy = -1; prev_ack = -1;
    for (int k = 0; k < 6; k++) begin
      logic [28:0] a;
      bsz_e        sz;
      logic [7:0]  be;
      a = BASE + 29'(320 + k * 9);                        // bytes, words and longwords at assorted lanes
      sz = k % 3 == 0 ? BZ_B : k % 3 == 1 ? BZ_W : BZ_L;
      if (sz == BZ_W) a[0] = 1'b0;
      if (sz == BZ_L) a[1:0] = 2'b00;
      be = sz == BZ_B ? 8'h01 << a[2:0] : sz == BZ_W ? 8'h03 << a[2:0] : 8'h0F << a[2:0];
      wbe[BRQ_OC_WTB] = be;
      for (int q = 0; q < 4; q++) wq[BRQ_OC_WTB][q] = {8{8'(8'h11 * (k + 1))}};
      x = m > prev_done ? m : prev_done;
      xact(BRQ_OC_WTB, x + 1, mk(1, a, sz, 0), o);
      ack = x + 2;
      chk(o.nwd == 1 && o.wc[0] == o.ready + 2 && o.wb[0] == a[4:3] && o.done == o.ready + 8,
          $sformatf("C8 store %0d: wd_req at ready + %0d (beat %0d), done at ready + %0d", k, o.wc[0] - o.ready,
                    o.wb[0], o.done - o.ready));
      if (k >= 2) chk(o.ready - prev_rdy == 10 && ack - prev_ack == 10,
                      $sformatf("C8 store stream: ready every %0d, ack every %0d, want 10", o.ready - prev_rdy,
                                ack - prev_ack));
      begin                                               // the beat landed at ready + 4, before done
        logic [63:0] want;
        want = pat(qi(a));
        for (int b = 0; b < 8; b++) if (be[b]) want[b*8 +: 8] = 8'(8'h11 * (k + 1));
        chk(mem[qi(a)] == want, $sformatf("C8 store %0d: memory %h, want %h", k, mem[qi(a)], want));
      end
      prev_done = o.done; prev_rdy = o.ready; prev_ack = ack;
      m = ack + 1;
    end
    idle(20);
    // SQ bursts (32-byte writes in address order) re-requested as soon as the previous one is done: 12 per burst;
    // the WBB's write-back of a line wraps from its first quadword like a fill
    prev_rdy = -1;
    for (int k = 0; k < 5; k++) begin
      logic [28:0] a;
      a = BASE + 29'(512 + k * 32 + (k == 4 ? 16 : 0));
      for (int q = 0; q < 4; q++) wq[k == 4 ? BRQ_OC_WBB : BRQ_SQ][q] = {32'(k), 32'(q)} ^ 64'h5555_0000_AAAA_0000;
      wbe[k == 4 ? BRQ_OC_WBB : BRQ_SQ] = 8'hFF;
      xact(k == 4 ? BRQ_OC_WBB : BRQ_SQ, k == 0 ? cyc + 2 : cyc + 1, mk(1, a, BZ_LINE, 0), o);
      chk(o.nwd == 4 && o.done == o.ready + 10, $sformatf("C8 burst %0d: %0d wd_req, done at ready + %0d", k, o.nwd,
                                                          o.done - o.ready));
      for (int j = 0; j < 4 && j < o.nwd; j++)
        chk(o.wc[j] == o.ready + 2 + 2 * j && o.wb[j] == 2'((int'(a[4:3]) + j) % 4),
            $sformatf("C8 burst %0d wd_req %0d at ready + %0d beat %0d", k, j, o.wc[j] - o.ready, o.wb[j]));
      if (k > 0 && k < 4) chk(o.ready - prev_rdy == 12, $sformatf("C8 SQ bursts: %0d per burst, want 12",
                                                                  o.ready - prev_rdy));
      prev_rdy = o.ready;
      idle(1);
      for (int q = 0; q < 4; q++)
        chk(mem[qi(a) - int'(a[4:3]) + q] == ({32'(k), 32'(q)} ^ 64'h5555_0000_AAAA_0000),
            $sformatf("C8 burst %0d quadword %0d: memory %h", k, q, mem[qi(a) - int'(a[4:3]) + q]));
    end
    idle(20);
    // 64-bit stores: two longword cycles, done at ready + 18, 20 per store back to back
    prev_rdy = -1;
    for (int k = 0; k < 3; k++) begin
      logic [28:0] a;
      a = BASE + 29'(704 + k * 8);
      wq[BRQ_OC_WTB][a[4:3]] = 64'hFEED_0000_0000_BEEF | 64'(k);
      wbe[BRQ_OC_WTB] = 8'hFF;
      xact(BRQ_OC_WTB, k == 0 ? cyc + 2 : cyc + 1, mk(1, a, BZ_Q, 0), o);
      chk(o.nwd == 1 && o.done == o.ready + 18, $sformatf("C8 Q store: %0d wd_req, done at ready + %0d", o.nwd,
                                                          o.done - o.ready));
      if (k > 0) chk(o.ready - prev_rdy == 20, $sformatf("C8 Q stores: %0d per store, want 20", o.ready - prev_rdy));
      prev_rdy = o.ready;
      idle(1);
      chk(mem[qi(a)] == (64'hFEED_0000_0000_BEEF | 64'(k)), $sformatf("C8 Q store %0d: memory %h", k, mem[qi(a)]));
    end
    idle(20);
  endtask

  // C8 TAS: a locked read and its WTB write hold the bus 32 Icyc from the read's accept
  task automatic s_tas();
    res_t rd, wr, nx, ic;
    wbe[BRQ_OC_WTB] = 8'h01;
    wq[BRQ_OC_WTB] = '{default: 64'h80};
    // eager pairs: the write asks in the Icyc after the read data, the next locked read right behind it
    xact(BRQ_OC_UNC, cyc + 2, mk(0, BASE + 800, BZ_B, 1), rd);
    for (int k = 0; k < 3; k++) begin
      fork
        xact(BRQ_OC_WTB, rd.bc[0] + 1, mk(1, BASE + 800, BZ_B, 0), wr);
        xact(BRQ_OC_UNC, rd.bc[0] + 3, mk(0, BASE + 800, BZ_B, 1), nx);
      join
      chk(wr.ready == rd.ready + 14, $sformatf("TAS pair %0d: write accepted at read + %0d, want 14", k,
                                               wr.ready - rd.ready));
      chk(nx.ready - rd.ready == 32, $sformatf("TAS: %0d per locked pair, want 32", nx.ready - rd.ready));
      rd = nx;
    end
    // finish the last pair; an IC read raised with the pair's write starts only when the hold ends
    fork
      xact(BRQ_OC_WTB, rd.bc[0] + 1, mk(1, BASE + 800, BZ_B, 0), wr);
      xact(BRQ_IC_UNC, rd.bc[0] + 1, mk(0, BASE + 832, BZ_L, 0), ic);
    join
    chk(wr.ready == rd.ready + 14 && ic.ready == rd.ready + 32,
        $sformatf("TAS: write at + %0d (want 14), a read waiting on the locked pair accepted at + %0d (want 32)",
                  wr.ready - rd.ready, ic.ready - rd.ready));
    idle(40);
    // a late write: the bus is held until its done + 2, beyond the 32
    xact(BRQ_OC_UNC, cyc + 2, mk(0, BASE + 800, BZ_B, 1), rd);
    fork
      xact(BRQ_OC_WTB, rd.bc[0] + 11, mk(1, BASE + 800, BZ_B, 0), wr);
      xact(BRQ_IC_UNC, rd.bc[0] + 1, mk(0, BASE + 832, BZ_L, 0), ic);
    join
    chk(wr.ready == rd.ready + 24 && ic.ready == wr.done + 2,
        $sformatf("TAS late write: write at read + %0d (want 24), the waiting read at write done + %0d (want 2)",
                  wr.ready - rd.ready, ic.ready - wr.done));
    idle(40);
  endtask

  // SOC.md 3.3 order, and the read overlap with a line's tail
  task automatic s_order();
    res_t bl, a, b;
    int   r;
    // (a) the WTB before a read raised in the same cycle
    wbe[BRQ_OC_WTB] = 8'h0F; wq[BRQ_OC_WTB] = '{default: 64'h0};
    fork
      xact(BRQ_OC_UNC, odd_at(cyc + 2, 1), mk(0, BASE + 900, BZ_L, 0), a);
      xact(BRQ_OC_WTB, odd_at(cyc + 2, 1), mk(1, BASE + 904, BZ_L, 0), b);
    join
    chk(b.ready < a.ready && a.ready == b.ready + 10, $sformatf("order: WTB at %0d, read at %0d (want WTB + 10)",
                                                                b.ready, a.ready));
    idle(40);
    // (b) the WTB overtakes an older read while an SQ burst holds the bus
    wbe[BRQ_SQ] = 8'hFF;
    fork
      xact(BRQ_SQ, cyc + 2, mk(1, BASE + 960, BZ_LINE, 0), bl);
      xact(BRQ_OC_UNC, cyc + 4, mk(0, BASE + 900, BZ_L, 0), a);
      xact(BRQ_OC_WTB, cyc + 6, mk(1, BASE + 904, BZ_L, 0), b);
    join
    chk(b.ready == bl.ready + 12 && a.ready == b.ready + 10,
        $sformatf("order: burst %0d, WTB %0d (want + 12), older read %0d (want WTB + 10)", bl.ready, b.ready, a.ready));
    idle(40);
    // (c) a fill and a write-back raised together: the fill first, the write-back at its critical beat (M3: the
    //     write-after-line rule; before M3 after its last beat, + 20)
    wbe[BRQ_OC_WBB] = 8'hFF;
    fork
      xact(BRQ_SQ, cyc + 2, mk(1, BASE + 960, BZ_LINE, 0), bl);
      xact(BRQ_OC_FILL, cyc + 4, mk(0, BASE + 1024, BZ_LINE, 0), a);
      xact(BRQ_OC_WBB, cyc + 4, mk(1, BASE + 1056, BZ_LINE, 0), b);
    join
    chk(a.ready == bl.ready + 12 && b.ready == a.ready + 12,
        $sformatf("order: fill at burst + %0d (want 12), write-back at fill + %0d (want 12)", a.ready - bl.ready,
                  b.ready - a.ready));
    idle(40);
    // (d) a read of the WBB's line waits for the WBB; a read of another line does not
    fork
      xact(BRQ_SQ, cyc + 2, mk(1, BASE + 960, BZ_LINE, 0), bl);
      xact(BRQ_OC_UNC, cyc + 4, mk(0, BASE + 1092, BZ_L, 0), a);
      xact(BRQ_OC_WBB, cyc + 4, mk(1, BASE + 1088, BZ_LINE, 0), b);
    join
    chk(b.ready == bl.ready + 12 && a.ready == b.ready + 12,
        $sformatf("order: WBB at burst + %0d (want 12), read of its line at WBB + %0d (want 12)", b.ready - bl.ready,
                  a.ready - b.ready));
    idle(40);
    fork
      xact(BRQ_SQ, cyc + 2, mk(1, BASE + 960, BZ_LINE, 0), bl);
      xact(BRQ_OC_UNC, cyc + 4, mk(0, BASE + 1124, BZ_L, 0), a);
      xact(BRQ_OC_WBB, cyc + 4, mk(1, BASE + 1088, BZ_LINE, 0), b);
    join
    chk(a.ready == bl.ready + 12 && b.ready == a.ready + 14,
        $sformatf("order: read of another line at burst + %0d (want 12), WBB at read + %0d (want 14)",
                  a.ready - bl.ready, b.ready - a.ready));
    idle(40);
    // (e) OC before IC on a tie; (f) otherwise the older first
    fork
      xact(BRQ_SQ, cyc + 2, mk(1, BASE + 960, BZ_LINE, 0), bl);
      xact(BRQ_IC_FILL, cyc + 4, mk(0, BASE + 1152, BZ_LINE, 0), a);
      xact(BRQ_OC_FILL, cyc + 4, mk(0, BASE + 1184, BZ_LINE, 0), b);
    join
    chk(b.ready == bl.ready + 12 && a.ready == b.ready + 14,
        $sformatf("order: tie: OC fill at %0d, IC fill at %0d (want OC first, IC + 14)", b.ready, a.ready));
    idle(40);
    fork
      xact(BRQ_SQ, cyc + 2, mk(1, BASE + 960, BZ_LINE, 0), bl);
      xact(BRQ_IC_FILL, cyc + 4, mk(0, BASE + 1152, BZ_LINE, 0), a);
      xact(BRQ_OC_FILL, cyc + 6, mk(0, BASE + 1184, BZ_LINE, 0), b);
    join
    chk(a.ready == bl.ready + 12 && b.ready == a.ready + 14,
        $sformatf("order: age: IC fill (older) at %0d, OC fill at %0d (want IC first, OC + 14)", a.ready, b.ready));
    idle(40);
    // (g) a read accepted in the Bcyc after the running line read's critical beat, while its tail beats still arrive
    //     (M3: + 14, PREF stream 14.35 [HW caches-sq 6.1]; before M3 at the critical beat, + 12)
    r = odd_at(cyc + 2, 1);
    fork
      xact(BRQ_OC_FILL, r, mk(0, BASE + 1216 + 8, BZ_LINE, 0), a);
      xact(BRQ_IC_FILL, r + 2, mk(0, BASE + 1248, BZ_LINE, 0), b);
    join
    chk(b.ready == a.ready + 14 && a.nb == 4 && a.bc[3] == a.ready + 18 && b.nb == 4 && b.bc[0] == b.ready + 12,
        $sformatf("overlap: second read at + %0d (want 14), first's last beat + %0d (18), second's first beat + %0d (12)",
                  b.ready - a.ready, a.bc[3] - a.ready, b.bc[0] - b.ready));
    for (int k = 0; k < 4; k++)
      chk(a.bq[k] == 2'((1 + k) % 4) && a.bd[k] == pat(qi(BASE + 1216) + (1 + k) % 4) && b.bq[k] == 2'(k) &&
          b.bd[k] == pat(qi(BASE + 1248) + k), $sformatf("overlap: beat %0d data", k));
    idle(40);
    // (h) a write of another line after a line read: accepted at its critical beat (M3, the write-after-line rule)
    wbe[BRQ_OC_WTB] = 8'hF0;
    fork
      xact(BRQ_OC_FILL, odd_at(cyc + 2, 1), mk(0, BASE + 1280, BZ_LINE, 0), a);
      xact(BRQ_OC_WTB, odd_at(cyc + 2, 1) + 2, mk(1, BASE + 1316, BZ_L, 0), b);
    join
    chk(b.ready == a.ready + 12, $sformatf("order: a write of another line after a line read at + %0d (want 12)",
                                           b.ready - a.ready));
    idle(40);
    // (i) a write of the line being read waits for its last beat
    fork
      xact(BRQ_OC_FILL, odd_at(cyc + 2, 1), mk(0, BASE + 1344 + 64, BZ_LINE, 0), a);
      xact(BRQ_OC_WTB, odd_at(cyc + 2, 1) + 2, mk(1, BASE + 1344 + 64 + 4, BZ_L, 0), b);
    join
    chk(b.ready == a.ready + 20, $sformatf("order: a write of the line being read at + %0d (want 20)",
                                           b.ready - a.ready));
    idle(40);
  endtask

  // resets: a manual reset lets the running transaction finish and accepts nothing new
  task automatic s_reset();
    res_t a, b;
    int   t;
    t = odd_at(cyc + 2, 1);
    fork
      xact(BRQ_OC_FILL, t, mk(0, BASE + 1344, BZ_LINE, 0), a);
      begin
        while (cyc < t + 1) @(posedge clk);
        @(negedge clk);
        rst.man = 1'b1;                                   // cycles t + 2 .. t + 15
        repeat (14) @(posedge clk);
        @(negedge clk);
        rst.man = 1'b0;
      end
      xact(BRQ_OC_UNC, t + 4, mk(0, BASE + 1380, BZ_L, 0), b);
    join
    chk(a.ready == t && a.done == t + 18, $sformatf("manual reset: the running line read done at + %0d (want 18)",
                                                    a.done - a.ready));
    chk(b.ready == t + 16, $sformatf("manual reset: a request raised during it accepted at %0d (want %0d)", b.ready,
                                     t + 16));
    idle(40);
  endtask

  // handshake: a requester that withdraws bq.v before ready is reported
  task automatic s_proto();
    res_t bl;
    chk(!proto_err, "protocol error flagged during the legal scenarios");
    $display("v1-bsc-agree tb: the next %%Error-STUB / %%Error-BIU lines (a withdrawn bq.v) are provoked on purpose");
    fork
      xact(BRQ_SQ, cyc + 2, mk(1, BASE + 960, BZ_LINE, 0), bl);
      begin
        idle(4);
        @(negedge clk);
        rq_d[BRQ_OC_UNC] = '{v: 1'b1, r: mk(0, BASE + 900, BZ_L, 0)};
        idle(2);
        @(negedge clk);
        rq_d[BRQ_OC_UNC] = '0;
      end
    join
    idle(2);
    chk(proto_err, "a withdrawn bq.v was not flagged");
  endtask

  initial begin
    rst = '0;
    rst.pin = 1'b1; rst.por = 1'b1;
    rq_d = '0;
    pq = '0;
    for (int i = 0; i < BRQ_N; i++) begin wbe[i] = '0; wq[i] = '{default: '0}; end
    repeat (4) @(posedge clk);
    @(negedge clk);
    rst = '0;
    idle(3);
    // the Dreamcast's BSC state (board.md 5.3) with the refresh timer stopped
    p4w(28'h080_0000, 2'd2, 32'hA302_0008);     // BCR1 (bits 31:29 are the MD straps)
    p4w(28'h080_0004, 2'd1, 32'h0000_0001);     // BCR2
    p4w(28'h080_0008, 2'd2, 32'h0111_0111);     // WCR1
    p4w(28'h080_000C, 2'd2, 32'h6180_66D8);     // WCR2
    p4w(28'h080_0010, 2'd2, 32'h0777_7777);     // WCR3
    p4w(28'h080_0014, 2'd2, 32'hC00A_0E24);     // MCR
    p4w(28'h080_001C, 2'd1, 32'h0000_A500);     // RTCSR: CKS = 0
    begin
      logic [31:0] v;
      p4r(28'h080_0000, 2'd2, v);
      chk(v == 32'hA302_0008, $sformatf("BCR1 reads %h after the setup", v));
      p4r(28'h080_0014, 2'd2, v);
      chk(v == 32'hC00A_0E24, $sformatf("MCR reads %h after the setup", v));
    end
    // open the test row (bank 0, row 2): one uncached read, then idle; the stub sees it too (no comparison yet)
    begin
      res_t o;
      xact(BRQ_IC_UNC, cyc + 2, mk(0, BASE, BZ_L, 0), o);
      idle(40);
    end
    cmp = 1'b1;
    s_c3c4();
    s_c5();
    s_c6();
    s_c7();
    s_c8();
    s_tas();
    s_order();
    s_reset();
    s_proto();
    cmp = 1'b0;
    idle(40);
    // the two memories
    begin
      int nd;
      nd = 0;
      for (int q = 0; q < MEMQ; q++) if (mem[q] != rmem[q]) begin
        nd++;
        if (nd <= 10) $display("FAIL: memory quadword %0d: stub %h real %h", q, mem[q], rmem[q]);
      end
      chk(nd == 0, $sformatf("%0d quadwords differ between the stub's memory and the board's", nd));
    end
    chk(n_bad_bus == 0, $sformatf("%0d bus cycles outside area 3 or outside bce cycles", n_bad_bus));
    chk(n_diff == 0, $sformatf("%0d strobe mismatches in %0d compared cycles", n_diff, n_cmp));
    n_chk += n_cmp;
    $display("v1-bsc-agree tb: %0d cycles compared, %0d strobe mismatches", n_cmp, n_diff);
    $display("v1-bsc-agree tb: %0d checks, %0d failures", n_chk, n_err);
    if (n_err != 0) $fatal(1, "v1-bsc-agree tb: FAIL");
    $display("v1-bsc-agree tb: PASS");
    $finish;
  end

  initial begin
    #400000;
    $fatal(1, "v1-bsc-agree tb: timeout");
  end
endmodule
/* verilator lint_on UNUSEDSIGNAL */
