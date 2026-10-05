#!/usr/bin/env python3
# check.py -- tb/bm/soc-design/check.py on this directory's expected.json (core-integration: ci, ci2, ci3).
#   usage: check.py RUN_OUTPUT [...] [--level core|fetch|soc|cache|all] [--prog P] [-v] | --self
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'soc-design'))
import check as _c
if '--expected' not in sys.argv:
    sys.argv[1:1] = ['--expected', os.path.join(HERE, 'expected.json')]
sys.exit(_c.main())
