// v1_bsc_stub_tb.sv -- unit check of tb/v1_bsc_stub.sv against rtl/v1/SOC.md 3.3.1 rows C3-C8 and the 3.3 / 3.3.2
// ordering and handshake rules (WI-0b).  Verilator --binary --timing; `make v1-bsc-stub`.
//
// Cycle convention: `cyc` counts Icyc; ce.bce is high in the odd cycles (FRQCR 0x0E0A, the last Icyc of each Bcyc).
// A driver process wakes at a rising edge and reads the outputs of the cycle that just ended (cyc); it changes the
// inputs at the following falling edge, i.e. for the next cycle (Verilator 5.014 runs a non-blocking assignment in an
// initial block as a blocking one, so the drives stay away from the rising edge).  Requester models, per SOC.md 3.3.1:
//   * a load miss / uncached load in MA1 at cycle m raises bq.v in m + 1 (C3) and is acked in the Icyc after its
//     critical rd_v (C6), so the penalty is ack - m;
//   * a store's WTB entry is loaded at the end of max(m, the previous write's done), raises bq.v in the next cycle and
//     acks the store one cycle after that (C8: "acks in m + 2 when its buffer is free");
//   * back to back: the next access's m is the previous ack + 1.
// Every check prints "FAIL: ..." when it fails; the run ends with "v1-bsc-stub tb: N checks, M failures" and exits 1 on
// a failure.  +stub_mut=K plants bug K in the stub: the run must then fail.
/* verilator lint_off UNUSEDSIGNAL */          // each scenario checks some fields of the result records; memory index bits
module v1_bsc_stub_tb import v1_soc_pkg::*; import v1_bsc_stub_pkg::*; ;
  logic clk = 1'b0;
  always #1 clk <= ~clk;

  int     cyc = 0;
  always @(posedge clk) cyc <= cyc + 1;

  rst_t   rst;
  clken_t ce;
  assign ce.bce = cyc[0];
  assign ce.pce = cyc[1:0] == 2'd3;

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

  // ---- the testbench memory: 32 KB of quadwords, index = addr[14:3]; initial value: its own address in each longword
  localparam int MEMQ = 4096;
  logic [63:0] mem [MEMQ];
  function automatic logic [63:0] pat(input int q);
    return {32'h8000_0000 | 32'(q * 8 + 4), 32'h4000_0000 | 32'(q * 8)};
  endfunction
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

  // rst.man high in cycles `from` .. from + n - 1 (called at or before the rising edge that ends cycle from - 1)
  task automatic man_reset(input int from, input int n);
    while (cyc < from - 1) @(posedge clk);
    chk(cyc == from - 1, $sformatf("manual reset from %0d requested at cycle %0d (scenario bug)", from, cyc));
    @(negedge clk);
    rst.man = 1'b1;
    repeat (n) @(posedge clk);
    @(negedge clk);
    rst.man = 1'b0;
  endtask

  // SOC.md 2.3 (I112): a manual reset ends a CPU bus lock, and a locked read presented across it is served unlocked
  task automatic s_reset_lock();
    res_t rd, wr, ic, bl;
    int   t;
    wbe[BRQ_OC_WTB] = 8'h01;
    wq[BRQ_OC_WTB] = '{default: 64'h80};
    wbe[BRQ_SQ] = 8'hFF;
    // (a) TAS.B's locked read accepted, then a manual reset before its write: the read completes, the pair's write
    // never comes, and a read raised during the reset is accepted in the first bce after it (a surviving lock admits
    // only the WTB: the bus would stay held for good)
    t = odd_at(cyc + 2, 1);
    fork
      xact(BRQ_OC_UNC, t, mk(0, BASE + 800, BZ_B, 1), rd);
      man_reset(t + 2, 14);                               // cycles t + 2 .. t + 15
      xact(BRQ_IC_UNC, t + 4, mk(0, BASE + 832, BZ_L, 0), ic);
    join
    chk(rd.ready == t && rd.done == t + 12, $sformatf("reset lock (a): the locked read ready + %0d, done + %0d (want 0, 12)",
                                                      rd.ready - t, rd.done - t));
    chk(ic.ready == t + 16, $sformatf("reset lock (a): a read raised during the reset accepted at + %0d (want 16)",
                                      ic.ready - t));
    idle(40);
    // (b) the pair's write accepted (the hold phase), then a manual reset: the hold ends with it; a read waiting on the
    // pair is accepted after the write's own occupancy (its done + 2), not at the locked read + 32
    t = odd_at(cyc + 2, 1);
    fork
      xact(BRQ_OC_UNC, t, mk(0, BASE + 800, BZ_B, 1), rd);
      xact(BRQ_OC_WTB, t + 13, mk(1, BASE + 800, BZ_B, 0), wr);
      xact(BRQ_IC_UNC, t + 13, mk(0, BASE + 832, BZ_L, 0), ic);
      man_reset(t + 16, 4);                               // cycles t + 16 .. t + 19
    join
    chk(rd.ready == t && wr.ready == t + 14 && wr.done == t + 22,
        $sformatf("reset lock (b): the pair read + %0d, write + %0d, write done + %0d (want 0, 14, 22)", rd.ready - t,
                  wr.ready - t, wr.done - t));
    chk(ic.ready == t + 24, $sformatf("reset lock (b): the waiting read accepted at read + %0d (want 24; 32 = held)",
                                      ic.ready - t));
    idle(40);
    // (c) a locked read raised while an SQ burst holds the bus and still presented across a manual reset is served
    // unlocked: accepted in the first bce after the reset, and the next read is accepted in the Bcyc after its critical
    // beat (bc 7; M3)
    t = odd_at(cyc + 2, 1);
    fork
      xact(BRQ_SQ, t, mk(1, BASE + 960, BZ_LINE, 0), bl);
      xact(BRQ_OC_UNC, t + 2, mk(0, BASE + 800, BZ_B, 1), rd);
      man_reset(t + 4, 14);                               // cycles t + 4 .. t + 17
      xact(BRQ_IC_UNC, t + 19, mk(0, BASE + 832, BZ_L, 0), ic);
    join
    chk(bl.ready == t && rd.ready == t + 18 && rd.done == rd.ready + 12,
        $sformatf("reset lock (c): burst + %0d, the locked read accepted + %0d (want 18), done at its ready + %0d (12)",
                  bl.ready - t, rd.ready - t, rd.done - rd.ready));
    chk(ic.ready == rd.ready + 14, $sformatf("reset lock (c): the next read accepted at the locked read + %0d (want 14)",
                                             ic.ready - rd.ready));
    idle(40);
    // (d) the mark ends with that accept: a locked read after the reset makes a pair again (32 per locked pair)
    t = odd_at(cyc + 2, 1);
    fork
      xact(BRQ_OC_UNC, t, mk(0, BASE + 800, BZ_B, 1), rd);
      xact(BRQ_OC_WTB, t + 13, mk(1, BASE + 800, BZ_B, 0), wr);
      xact(BRQ_IC_UNC, t + 13, mk(0, BASE + 832, BZ_L, 0), ic);
    join
    chk(wr.ready == t + 14 && ic.ready == t + 32,
        $sformatf("reset lock (d): write at read + %0d (want 14), the waiting read at + %0d (want 32)", wr.ready - t,
                  ic.ready - t));
    idle(40);
  endtask

  // handshake: a requester that withdraws bq.v before ready is reported
  task automatic s_proto();
    res_t bl;
    chk(!proto_err, "protocol error flagged during the legal scenarios");
    $display("v1-bsc-stub tb: the next %%Error-STUB line (a withdrawn bq.v) is provoked on purpose");
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
    for (int i = 0; i < BRQ_N; i++) begin wbe[i] = '0; wq[i] = '{default: '0}; end
    repeat (4) @(posedge clk);
    @(negedge clk);
    rst = '0;
    idle(3);
    s_c3c4();
    s_c5();
    s_c6();
    s_c7();
    s_c8();
    s_tas();
    s_order();
    s_reset();
    s_reset_lock();
    s_proto();
    $display("v1-bsc-stub tb: %0d checks, %0d failures", n_chk, n_err);
    if (n_err != 0) $fatal(1, "v1-bsc-stub tb: FAIL");
    $display("v1-bsc-stub tb: PASS");
    $finish;
  end

  initial begin
    #200000;
    $fatal(1, "v1-bsc-stub tb: timeout");
  end
endmodule
/* verilator lint_on UNUSEDSIGNAL */
