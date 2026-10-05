# Step calculator — FIPR / FTRV / FSCA / FSRRA on the shared datapath

`index.html` walks your own operands through the SH7091 vector block one cycle at a time: pick the instruction with
the tabs, type the values, and step with **next / back** (or ← →, shift for four at a time). The diagram is the one
from `blog/fpu/img/anim_*.svg`; the right-hand pane logs every value as it appears, named by the part of the circuit
that produced it — decode, each multiplier, the feedback, each aligner slot, the adder, the rounding stage.

Open the file directly (`file://`), no build step and no server. `#fsca/4` in the URL selects a tab and a cycle, and
the URL updates as you step, so a particular view can be linked.

| file | what it is |
|---|---|
| `index.html` | the page: diagram, controls, log |
| `engine.js` | bit-exact model ported from `rtl/fpu_vec/*.sv`: the truncated Booth array, the aligners, the four-input adder, the rounding stage, and the four microprograms; returns a record per cycle |
| `tables.js` | GENERATED — the FSCA and FSRRA coefficient ROMs, read out of `rtl/fpu_vec/fpu_vec_tables.svh` |
| `tools/gen_tables.py` | regenerates `tables.js` (run it after any ROM change) |
| `tools/ref.c`, `tools/test_engine.js` | differential test of `engine.js` against `model/sh4_fpu_approx.h` |

```sh
python3 blog/calculator/tools/gen_tables.py                     # refresh the ROMs from the RTL
gcc -O2 -o /tmp/ref blog/calculator/tools/ref.c                 # the C reference
node blog/calculator/tools/test_engine.js 3000                  # 18,788 comparisons, 0 mismatches
```

Values are shown the way the hardware holds them: the input latches and product registers are integers on the
block's internal grid (hex, signed), the sum is the 35-bit adder output, and only the result register is an IEEE
single. Inputs accept a decimal float (`1.5`, `3.14159`) or a raw bit pattern (`0x3fc00000`); FSCA's angle takes
`0x2345` or `9029`.

Defaults are chosen to be off the table nodes — an angle or mantissa that lands exactly on a node has `dx = 0`, and
then every product in the trace is zero.
