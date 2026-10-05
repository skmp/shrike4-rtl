/* bscmm2: the console build's addresses (tools/hw/bsc-memmap/bscmm2.elf; bm.ld) */
BM_PROG_TEXT   = 0x8C01022C;   /* _meas; _main 0x8C01036C, _t_rd_l_0 0x8C010BA0 */
BM_KOSIRQ      = 0x8C0408D0;   /* _irq_disable 0x8C0408D2, _irq_restore 0x8C0408FC */
BM_PROG_RODATA = 0x8C05B130;   /* _hi.0 (the object's first read-only datum), then the kern_t K_* */
_bm_alloc_0    = 0x8C083A80;   /* ubuf (B ubuf ac083a80) */
_bm_alloc_1    = 0x8C093AC0;   /* cbuf */
_bm_alloc_2    = 0x8C0A3B00;   /* big */
_bm_nalloc     = 3;
INCLUDE bm.ld
