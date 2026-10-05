# Hardware dumps for the SH4 FPU reverse-engineering

Captured from a real Dreamcast over dcload-ip with `fpu_dump.c` / `fpu_dump.elf` in this directory.
All files are raw little-endian u32 arrays with no header.

| file | size | contents |
|---|---|---|
| `fsca_rnrz.bin` | 1 MB | 65536 records {sin_rn, cos_rn, sin_rz, cos_rz}, one per 16-bit FSCA angle, captured with FPSCR.RM = nearest and = zero. The RN/RZ pair pins the internal 2^-26 value to a small window per sample. **This is the whole FSCA ground truth** — see `../../fsca/CURRENT.md` 2 for why the cosine half adds nothing. |
| `fsrra_rz.bin` | 64 MB | 2^24 FSRRA results in RZ over mantissas [1,4), indexed by mantissa. |
| `fsrra_rn_mantissa_1to4.bin` | 64 MB | the matching RN mantissas, extracted from the 8.5 GB full-range RN dump. Paired with `fsrra_rz.bin` this pins FSRRA's internal value per sample the same way the FSCA pair does — 2^24 samples, 512x the FSCA count. Moved here from a session scratch directory on 2026-09-18 so the FSRRA harness (`../../fsrra/tools/j*.cpp`) is self-contained. |
| `fipr_rnrz.bin` (in `../kos_fipr_dump/`) | | FIPR vectors in both rounding modes; FIPR is finished and bit-exact. |

The 8.5 GB full-range FSRRA RN dump (`fsrra_0x00800000-0x7f800000.dump`, one float per positive normal input)
is **not** in this tree. `fsrra_rn_mantissa_1to4.bin` is the part of it that the mantissa analysis needs; if the
full file is wanted again it has to be re-captured or located on the user's machine.
