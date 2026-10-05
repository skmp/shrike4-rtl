#!/usr/bin/env python3
"""v1_fpuhold_bm.py -- make v1-fpuhold, part 4: the console's core-integration probes (ci, ci2, ci3; tb/bm/
core-integration) replayed on the core (v1-exc's harness: v1_core on v1_ideal_mem with the Iclk-class P4 stub), checked
with tb/bm/soc-design/check.py at the level `fpu` = core + fetch + the tokens WI-CORE-A marked "fpu" for this work item
(WI-CORE-F: the FPU kill, the F1 operand trap, the hold split): ci pair_ld_fadd / pair_fadd_add / pair_add_fadd /
fdiv_trap / fpu_z records and the fpu_v / fdiv+fpu_v loops, ci2 frchg_kill / fdiv_cause records and the vbyp / vfar
loops, ci3 p4_fdiv (15.0: the divider runs under `mah`).  Every compared number is the console's.
    usage: v1_fpuhold_bm.py RUN_OUTPUT [...] [--prog ci,ci2,ci3] [-v]
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'bm', 'soc-design'))
import check as _c
_c.LEVELS['fpu'] = {'core', 'fetch', 'fpu'}
args = sys.argv[1:]
if '--expected' not in args:
    args = ['--expected', os.path.join(HERE, 'bm', 'core-integration', 'expected.json')] + args
if '--level' not in args:
    args += ['--level', 'fpu']
sys.argv = [sys.argv[0]] + args
sys.exit(_c.main())
