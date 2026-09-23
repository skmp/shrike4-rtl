#!/usr/bin/env python3
"""Generate rtl/timing-proxy/tp_decode.sv: the operand decoder of the timing proxy.

The instruction forms, their encodings and operands come from the sweep's ISA table (model/timings2/isa.py), so the
proxy decodes exactly the instruction set that was measured.  Decoding is (opcode, FPSCR.PR, FPSCR.SZ) -> one form;
the operands become register masks by role (what the D stage compares against the pipeline):

  GPR   ga  address base / index         gd  store data              gs  other sources
        gwl load destination             gwu address update (@Rm+, @-Rn)                gw  other destinations
  bank  bkr / bkw  R0_BANK..R7_BANK of the other bank (LDC / STC Rn_BANK)
  FP    fs  operands                     fd  store data              fw  destinations   (32 physical: bank*16 + i;
        FPSCR.FR selects the bank of FRn / DRn / FVn, XDn / XMTRX are the other bank)
  sys   sr / sw  over SYS_* (tp_pkg): T S QM SR GBR VBR SSR SPC SGR DBR MACH MACL PR FPUL FPSCR_M FPSCR_C TLB

The timing of each form (issue steps, the stage each operand is needed / each result is ready, the units it holds)
is not here: it is the form's entry in the hand-written timing ROM (tp_timing.svh).
usage: gen_decode.py [out.sv]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'model', 'timings2'))
import isa  # noqa: E402

SYS = ['T', 'S', 'QM', 'SR', 'GBR', 'VBR', 'SSR', 'SPC', 'SGR', 'DBR', 'MACH', 'MACL', 'PR', 'FPUL', 'FPSCR_M',
       'FPSCR_C', 'TLB']


def form_list():
    """one decode entry per distinct (encoding, modes): the taken / not-taken twins are one form here"""
    out, seen = [], {}
    for f in isa.FORMS:
        base = f.name.replace(' (taken)', '').replace(' (not taken)', '')
        if base in seen:
            continue
        seen[base] = f
        out.append((base, f))
    return out


def ident(name):
    s = name.upper()
    for a, b in (('@(DISP,', 'AT_D_'), ('@(R0,', 'AT_R0_'), ('@-', 'AT_PRE_'), ('@', 'AT_'), ('+', '_INC'),
                 ('#IMM', 'IMM'), ('/', '_'), ('.', '_'), (',', '_'), (' ', '_'), ('(', ''), (')', '')):
        s = s.replace(a, b)
    while '__' in s:
        s = s.replace('__', '_')
    return 'F_' + s.strip('_')


def field(f, ch):
    sh, wd = f.fields[ch]
    return f'op[{sh + wd - 1}:{sh}]'


def gpr_expr(f, name):
    if name == 'R0':
        return "16'h0001"
    return f"(16'h0001 << {field(f, name[1])})"


def fr_expr(f, name):
    """32-bit physical FR mask expression; fb = FPSCR.FR (md[2])"""
    if name == 'FRn' or name == 'FRm':
        return f"fr1(fb, {field(f, name[2])})"
    if name == 'FR0':
        return "fr1(fb, 4'd0)"
    if name in ('DRn', 'DRm'):
        return f"fr2(fb, {{{field(f, name[2])}, 1'b0}})"          # the 3-bit field is DR number / 2
    if name in ('XDn', 'XDm'):
        return f"fr2(~fb, {{{field(f, name[2])}, 1'b0}})"
    if name in ('FVn', 'FVm'):
        return f"fr4(fb, {{{field(f, name[2])}, 2'b00}})"
    if name == 'FIPRn':
        return f"fr1(fb, {{{field(f, 'n')}, 2'b11}})"
    if name == 'XMTRX':
        return "(fb ? 32'h0000_ffff : 32'hffff_0000)"
    raise KeyError(name)


def sys_bit(name):
    m = {'FRB': 'FPSCR_M', 'SZB': 'FPSCR_M', 'PTE': 'TLB'}
    return 'SYS_' + m.get(name, name)


def roles(f):
    """{role: [expr, ...]} for one form"""
    r = {k: [] for k in ('ga', 'gd', 'gs', 'gwl', 'gwu', 'gw', 'bkr', 'bkw', 'fs', 'fd', 'fw', 'sr', 'sw')}
    mem_bases = {}
    for m in f.mems:
        if m.base in ('Rn', 'Rm'):
            mem_bases[m.base] = m
    store = any(m.kind in ('S', 'A') for m in f.mems)
    load = any(m.kind in ('L', 'M', 'T') for m in f.mems)
    for op in f.ops:
        nm, acc = op.name, op.acc
        rd, wr = 'r' in acc, 'w' in acc
        if nm in ('Rn', 'Rm', 'R0'):
            e = gpr_expr(f, nm)
            if nm in mem_bases:
                m = mem_bases[nm]
                r['ga'].append(e)
                if m.mode in ('post', 'pre'):
                    r['gwu'].append(e)
                continue
            if nm == 'R0' and any(m.mode in ('r0', 'gbr0') for m in f.mems):
                # R0 as the index of @(R0,Rx) / @(R0,GBR) (no form has R0 as data and as index)
                r['ga'].append(e)
                continue
            if rd:
                (r['gd'] if store and not wr else r['gs']).append(e)
            if wr:
                (r['gwl'] if load and not f.br else r['gw']).append(e)
            continue
        if nm in ('BANKn', 'BANKm'):
            e = f"(8'h01 << {field(f, nm[4])})"
            (r['bkw'] if wr else r['bkr']).append(e)
            continue
        if nm in ('FRn', 'FRm', 'FR0', 'DRn', 'DRm', 'XDn', 'XDm', 'FVn', 'FVm', 'FIPRn', 'XMTRX'):
            e = fr_expr(f, nm)
            if rd:
                (r['fd'] if store and not wr else r['fs']).append(e)
            if wr:
                r['fw'].append(e)
            continue
        b = sys_bit(nm)
        if nm == 'FPSCR':
            # every FP instruction reads the mode; the arithmetic ones ('+FPSCR') also update the cause / flags
            if rd:
                r['sr'].append('SYS_FPSCR_M')
            if acc == 'rw':
                r['sr'].append('SYS_FPSCR_C')
                r['sw'].append('SYS_FPSCR_C')
            elif wr:            # LDS / LDS.L to FPSCR
                r['sw'] += ['SYS_FPSCR_M', 'SYS_FPSCR_C']
            continue
        if nm == 'SR':
            if rd:
                r['sr'] += ['SYS_SR']
            if wr:
                r['sw'] += ['SYS_SR']
            continue
        if rd:
            r['sr'].append(b)
        if wr:
            r['sw'].append(b)
    # GBR-relative memory operands read GBR
    for m in f.mems:
        if m.mode in ('gbr', 'gbr0') and 'SYS_GBR' not in r['sr']:
            r['sr'].append('SYS_GBR')
    # STS FPSCR / STS.L FPSCR read the whole register; LDS / LDS.L FPSCR are decoded by the FPU too (they wait for
    # the mode an older LDS FPSCR loads, like every FPU-decoded instruction)
    if f.name.startswith('STS') and 'FPSCR' in f.name:
        r['sr'] = ['SYS_FPSCR_M', 'SYS_FPSCR_C']
    if f.name.startswith('LDS') and 'FPSCR' in f.name:
        r['sr'].append('SYS_FPSCR_M')
    # measured decode quirks (timings2 sweep; see README.md):
    #  * SWAP.B / SWAP.W interlock on Rn as if they read it (a load into Rn holds them 2 cycles)
    #  * LDC Rm,SR and LDC Rm,GBR also interlock on R[op[7:4]] (R0 / R1), a coarse minor-opcode decode
    #  * FABS DRn / FNEG DRn touch only FR(2n), the sign word: an LS op on one register
    if f.name.startswith('SWAP.'):
        r['gs'].append(gpr_expr(f, 'Rn'))
    if f.name in ('LDC Rm,SR', 'LDC Rm,GBR'):
        r['gs'].append("(16'h0001 << op[7:4])")
    if f.name in ('FABS DRn', 'FNEG DRn'):
        e = f"fr1(fb, {{{field(f, 'n')}, 1'b0}})"
        r['fs'], r['fw'] = [e], [e]
    return r


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'tp_decode.sv')
    forms = form_list()
    L = []
    L.append('// GENERATED by gen_decode.py from model/timings2/isa.py -- do not edit.')
    L.append('//')
    L.append('// Operand decoder of the timing proxy: (opcode, FPSCR.FR / SZ / PR) -> form number and register masks by role.')
    L.append('// See gen_decode.py for the roles.  One wildcard compare per form (with its FPU-mode condition); no two match together in any mode.')
    L.append('module tp_decode import tp_pkg::*; (')
    L.append('  input  logic [15:0] op,')
    L.append('  input  logic [2:0]  md,        // {FPSCR.FR, FPSCR.SZ, FPSCR.PR} while the instruction executes')
    L.append('  output dec_t        d')
    L.append(');')
    L.append('  wire fb = md[2], sz = md[1], pr = md[0];')
    L.append('  function automatic logic [31:0] fr1(input logic b, input logic [3:0] i); return 32\'h1 << {b, i}; endfunction')
    L.append('  function automatic logic [31:0] fr2(input logic b, input logic [3:0] i); return 32\'h3 << {b, i}; endfunction')
    L.append('  function automatic logic [31:0] fr4(input logic b, input logic [3:0] i); return 32\'hf << {b, i}; endfunction')
    L.append('  always_comb begin')
    L.append("    d = '0;")
    # a flat chain of wildcard compares: forms of different FPU modes overlap in their bit patterns (FMOV FRm,FRn /
    # FMOV DRm,DRn), so the mode condition is part of each arm; no two arms match together in any mode
    enums = []
    first = True
    for base, f in forms:
        pat = f.enc.replace('n', '?').replace('m', '?').replace('i', '?').replace('d', '?')
        pr_ok = sorted({isa.MODES[m][0] for m in f.modes})
        sz_ok = sorted({isa.MODES[m][1] for m in f.modes})
        cond = [f"op ==? 16'b{pat}"]
        if pr_ok != [0, 1]:
            cond.append(f"pr == 1'b{pr_ok[0]}")
        if sz_ok != [0, 1]:
            cond.append(f"sz == 1'b{sz_ok[0]}")
        kw = 'if' if first else 'else if'
        first = False
        L.append(f"    {kw} ({' && '.join(cond)}) begin  // {base}")
        L.append(f'      d.valid = 1\'b1; d.form = {ident(base)};')
        enums.append((ident(base), base))
        for role, exprs in roles(f).items():
            if not exprs:
                continue
            if role in ('sr', 'sw'):
                L.append(f'      d.{role} = ' + ' | '.join(f"(SYSW'(1) << {e})" for e in exprs) + ';')
            else:
                L.append(f'      d.{role} = ' + ' | '.join(exprs) + ';')
        L.append('    end')
    by_enc = {f.enc for _, f in forms}
    L.append('  end')
    L.append('endmodule')
    open(out, 'w').write('\n'.join(L) + '\n')
    # the form enumeration for tp_pkg
    enum_out = os.path.join(os.path.dirname(out), 'tp_forms.svh')
    E = ['// GENERATED by gen_decode.py -- do not edit.  Form numbers (0 = unknown).', 'typedef enum logic [7:0] {',
         '  F_NONE = 8\'d0,']
    for k, (e, base) in enumerate(enums):
        E.append(f"  {e} = 8'd{k + 1}{',' if k + 1 < len(enums) else ''}  // {base}")
    E.append('} form_e;')
    E.append('localparam int SYSN = %d;' % len(SYS))
    E.append('typedef enum int {' + ', '.join(f'SYS_{s} = {i}' for i, s in enumerate(SYS)) + '} sys_e;')
    open(enum_out, 'w').write('\n'.join(E) + '\n')
    # a name table for the testbench / reports
    open(os.path.join(os.path.dirname(out), 'tp_forms.txt'), 'w').write(
        '\n'.join(f'{k + 1} {base}' for k, (e, base) in enumerate(enums)) + '\n')
    print(f'{len(enums)} forms, {len(by_enc)} encodings -> {out}', file=sys.stderr)


if __name__ == '__main__':
    main()
