/* bscmm: the console build's addresses (tools/hw/bsc-memmap/bscmm.elf; bm.ld).
 * The ELF in tools/hw is not the one that printed bscmm.elf.hwlog: the log is from 04:05:53, the ELF from 04:10:28, after
 * gen_tests.py (04:09:05) added run 2's kernels (rd_wr, tas, rd_x16, c_p*: 0xF40 bytes at the end of tests.S, after
 * every kernel run 1 uses).  The run's image had KOS's code and read-only data 0xF40 lower: irq_disable at 0x8C03FFF2,
 * whose IC lines (indices 0xFF, 0x00) are t_c_rd_24's first two -- the console's oc_hit / oc_miss c_rd K = 24 columns
 * (+65 / +83 over the steady rate; mkexpected.py's "instruction-cache conflicts") -- where the 04:10 image's
 * (index 0x79) meets no kernel.  layout.py --shift checks the two groups at the run's addresses */
BM_PROG_TEXT   = 0x8C01022C;   /* _dump_regs; _meas 0x8C01031C, _main 0x8C010490, _t_rd_l_0 0x8C011200 */
BM_KOSIRQ      = 0x8C03FFF0;   /* _irq_disable 0x8C03FFF2, _irq_restore 0x8C04001C (the ELF's 0x8C040F32 - 0xF40) */
BM_PROG_RODATA = 0x8C05AD6C;   /* _K_sq_pref_nd, the first kern_t (the ELF's 0x8C05BCAC - 0xF40) */
_bm_alloc_0    = 0x8C083BC0;   /* ubuf (B ubuf ac083bc0) */
_bm_alloc_1    = 0x8C093C00;   /* cbuf */
_bm_alloc_2    = 0;
_bm_nalloc     = 2;
INCLUDE bm.ld
