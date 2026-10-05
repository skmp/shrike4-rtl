#!/usr/bin/env python3
"""Golden vectors for rtl/v1 unit A (v1_alu_pkg / v1_mul_pkg) from the sweep's functional model.

Every integer form of model/timings2/isa.py that is an ALU operation (and the five MUL / DMUL forms) is executed by
isa.py's execute() on a state that holds the operands; the ALU / multiplier inputs are derived from that state the way
the core's decoder presents them (ARCH.md section 3, unit A: a = Rm side or the extended #imm, b = Rn side / R0 /
the loaded byte), and the expected outputs are read back from the state afterwards.  Coverage per form:
    Rm,Rn forms      edge x edge pairs (every flag combination for the T / Q / M readers) + 20000 random
    Rn forms         edges x 8 flag combinations + 20000 random (a = random garbage the op must ignore)
    #imm forms       all 256 immediates x 64 register values
    #imm,@(R0,GBR)   all 256 immediates x all 256 memory bytes (AND.B / OR.B / XOR.B / TST.B), TAS.B all 256 bytes
    SHAD / SHLD      all 64 (Rm[31], Rm[4:0]) x 128 Rn values, half of them with garbage in Rm[30:5]
    CMP/STR          all 16 byte-equality masks x 1000
MAC.W / MAC.L are 'unknown' in isa.py (their values are the manual's C code, checked in the C++ testbench).

Output (argv[1], default build/v1_alu):
  v1_alu_ops.h  the alu_op_e / mul_op_e encodings parsed from rtl/v1/v1_alu.sv / v1_mul.sv (the single source of the
                numbering; the testbench includes it)
  vectors.bin   little endian:
      "V1AL" u32 nform, nform x { u8 len, name }
      u32 nalu,  nalu  x { u16 form, u8 op, u8 fin (t | q<<1 | m<<2), u32 a, u32 b, u32 r,
                           u8 fexp (t | q<<1 | m<<2 | wr_t<<3 | wr_qm<<4 | wr_r<<5 | r_byte<<6), 3 pad }
      u32 nmul,  nmul  x { u16 form, u8 op, u8 s, u32 a, u32 b, u64 acc, u64 res }
  wr_r = the form writes the GPR (r is then compared; r_byte: only r[7:0], the byte stored back to memory).
"""
import os
import random
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'model', 'timings2'))
import isa  # noqa: E402

M32 = 0xFFFFFFFF


def parse_enum(path, tname):
    src = open(path).read()
    src = re.sub(r'//[^\n]*', '', src)
    m = re.search(r'typedef\s+enum\s+logic\s*\[[^\]]*\]\s*\{([^}]*)\}\s*' + tname, src)
    assert m, (path, tname)
    return [x.strip() for x in m.group(1).split(',') if x.strip()]


ALU_OPS = parse_enum(os.path.join(ROOT, 'rtl', 'v1', 'v1_alu.sv'), 'alu_op_e')
MUL_OPS = parse_enum(os.path.join(ROOT, 'rtl', 'v1', 'v1_mul.sv'), 'mul_op_e')

# isa.py form -> (ALU op, operand kind)
#   rr   Rm,Rn: a = Rm, b = Rn            rn  Rn: b = Rn, a = garbage        mrn  Rm -> Rn: a = Rm, b = old Rn
#   isn  #imm,Rn (a = sx8)                isr0 #imm,R0 with sx8 (CMP/EQ)     izr0 #imm,R0 with zero-extended imm
#   none no operand (a, b garbage)        rmw  #imm,@(R0,GBR): a = imm, b = the byte   tas  TAS.B @Rn: b = the byte
ALU_FORMS = [
    ('ADD Rm,Rn', 'A_ADD', 'rr'), ('ADD #imm,Rn', 'A_ADD', 'isn'), ('ADDC Rm,Rn', 'A_ADDC', 'rr'),
    ('ADDV Rm,Rn', 'A_ADDV', 'rr'), ('SUB Rm,Rn', 'A_SUB', 'rr'), ('SUBC Rm,Rn', 'A_SUBC', 'rr'),
    ('SUBV Rm,Rn', 'A_SUBV', 'rr'), ('NEG Rm,Rn', 'A_NEG', 'mrn'), ('NEGC Rm,Rn', 'A_NEGC', 'mrn'),
    ('AND Rm,Rn', 'A_AND', 'rr'), ('AND #imm,R0', 'A_AND', 'izr0'), ('AND.B #imm,@(R0,GBR)', 'A_AND', 'rmw'),
    ('OR Rm,Rn', 'A_OR', 'rr'), ('OR #imm,R0', 'A_OR', 'izr0'), ('OR.B #imm,@(R0,GBR)', 'A_OR', 'rmw'),
    ('XOR Rm,Rn', 'A_XOR', 'rr'), ('XOR #imm,R0', 'A_XOR', 'izr0'), ('XOR.B #imm,@(R0,GBR)', 'A_XOR', 'rmw'),
    ('NOT Rm,Rn', 'A_NOT', 'mrn'),
    ('TST Rm,Rn', 'A_TST', 'rr'), ('TST #imm,R0', 'A_TST', 'izr0'), ('TST.B #imm,@(R0,GBR)', 'A_TST', 'rmw'),
    ('TAS.B @Rn', 'A_TAS', 'tas'),
    ('CMP/EQ Rm,Rn', 'A_CMPEQ', 'rr'), ('CMP/EQ #imm,R0', 'A_CMPEQ', 'isr0'), ('CMP/HS Rm,Rn', 'A_CMPHS', 'rr'),
    ('CMP/GE Rm,Rn', 'A_CMPGE', 'rr'), ('CMP/HI Rm,Rn', 'A_CMPHI', 'rr'), ('CMP/GT Rm,Rn', 'A_CMPGT', 'rr'),
    ('CMP/PZ Rn', 'A_CMPPZ', 'rn'), ('CMP/PL Rn', 'A_CMPPL', 'rn'), ('CMP/STR Rm,Rn', 'A_CMPSTR', 'rr'),
    ('DIV0S Rm,Rn', 'A_DIV0S', 'rr'), ('DIV0U', 'A_DIV0U', 'none'), ('DIV1 Rm,Rn', 'A_DIV1', 'rr'),
    ('DT Rn', 'A_DT', 'rn'),
    ('EXTS.B Rm,Rn', 'A_EXTSB', 'mrn'), ('EXTS.W Rm,Rn', 'A_EXTSW', 'mrn'), ('EXTU.B Rm,Rn', 'A_EXTUB', 'mrn'),
    ('EXTU.W Rm,Rn', 'A_EXTUW', 'mrn'), ('SWAP.B Rm,Rn', 'A_SWAPB', 'mrn'), ('SWAP.W Rm,Rn', 'A_SWAPW', 'mrn'),
    ('XTRCT Rm,Rn', 'A_XTRCT', 'rr'), ('MOV Rm,Rn', 'A_MOV', 'mrn'), ('MOV #imm,Rn', 'A_MOV', 'isn'),
    ('MOVT Rn', 'A_MOVT', 'rn'),
    ('ROTL Rn', 'A_ROTL', 'rn'), ('ROTR Rn', 'A_ROTR', 'rn'), ('ROTCL Rn', 'A_ROTCL', 'rn'),
    ('ROTCR Rn', 'A_ROTCR', 'rn'), ('SHAL Rn', 'A_SHAL', 'rn'), ('SHAR Rn', 'A_SHAR', 'rn'),
    ('SHLL Rn', 'A_SHLL', 'rn'), ('SHLR Rn', 'A_SHLR', 'rn'), ('SHLL2 Rn', 'A_SHLL2', 'rn'),
    ('SHLR2 Rn', 'A_SHLR2', 'rn'), ('SHLL8 Rn', 'A_SHLL8', 'rn'), ('SHLR8 Rn', 'A_SHLR8', 'rn'),
    ('SHLL16 Rn', 'A_SHLL16', 'rn'), ('SHLR16 Rn', 'A_SHLR16', 'rn'),
    ('SHAD Rm,Rn', 'A_SHAD', 'rr'), ('SHLD Rm,Rn', 'A_SHLD', 'rr'),
    ('CLRT', 'A_CLRT', 'none'), ('SETT', 'A_SETT', 'none'),
]
MUL_FORMS = [('MUL.L Rm,Rn', 'M_MULL'), ('MULS.W Rm,Rn', 'M_MULSW'), ('MULU.W Rm,Rn', 'M_MULUW'),
             ('DMULS.L Rm,Rn', 'M_DMULS'), ('DMULU.L Rm,Rn', 'M_DMULU')]

# the forms whose result depends on the incoming T / Q / M
FLAG_READERS = {'A_ADDC', 'A_SUBC', 'A_NEGC', 'A_DIV1', 'A_ROTCL', 'A_ROTCR', 'A_MOVT'}

EDGES = sorted({0, 1, 2, 3, 0x7F, 0x80, 0x81, 0xFF, 0x100, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x10000, 0x7FFFFF,
                0x800000, 0xFFFFFF, 0x1000000, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x80000002,
                0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFD, 0xFFFFFF80, 0xFFFFFF7F, 0xFFFF8000, 0xFFFF7FFF, 0xFFFF0000,
                0x0000FF00, 0x00FF0000, 0xFF000000, 0x55555555, 0xAAAAAAAA, 0x12345678, 0x87654321, 0x0F0F0F0F,
                0xF0F0F0F0})
MEMBASE = 0x0C001000
PC = 0x8C010000


def rnd32(rng):
    """random operand: uniform, or small / near-power-of-two magnitude, either sign"""
    k = rng.randrange(4)
    if k == 0:
        return rng.getrandbits(32)
    if k == 1:
        return rng.getrandbits(rng.randrange(1, 33))
    if k == 2:
        return (-rng.getrandbits(rng.randrange(1, 33))) & M32
    return ((1 << rng.randrange(32)) + rng.randrange(-2, 3)) & M32


def base_state(rng):
    st = isa.State()
    for i in range(16):
        st.reg[f'r{i}'] = rng.getrandbits(32)
    st.reg.update({'t': 0, 'qm': 0, 's': 0, 'mach': rng.getrandbits(32), 'macl': rng.getrandbits(32),
                   'gbr': MEMBASE, 'src': 0x400000F0})
    st.mem_ok = [(MEMBASE, MEMBASE + 0x100, 'rw')]
    return st


def run_alu(rng, form, op, kind, a, b, t, q, m, imm=0, byte=0):
    """one vector through isa.execute(); returns the packed record"""
    st = base_state(rng)
    st.reg['t'], st.reg['qm'] = t, q | (m << 1)
    f = {}
    rn = 'r1'
    if kind in ('rr', 'mrn'):
        f = {'n': 1, 'm': 2}
        st.reg['r1'], st.reg['r2'] = b, a
    elif kind == 'rn':
        f = {'n': 1}
        st.reg['r1'] = b
    elif kind in ('isn',):
        f = {'n': 1, 'i': imm}
        st.reg['r1'] = b
        a = isa.sx8(imm) & M32
    elif kind in ('isr0', 'izr0'):
        f = {'i': imm}
        rn = 'r0'
        st.reg['r0'] = b
        a = (isa.sx8(imm) & M32) if kind == 'isr0' else imm
    elif kind == 'rmw':
        f = {'i': imm}
        st.reg['r0'] = 0x40
        st.mem[MEMBASE + 0x40] = byte
        a, b = imm, byte
    elif kind == 'tas':
        f = {'n': 1}
        st.reg['r1'] = MEMBASE + 0x20
        st.mem[MEMBASE + 0x20] = byte
        b = byte
    st.trace_w = set()
    isa.execute(st, form, f, PC)
    w = st.trace_w
    fexp = 0
    r = 0
    if kind in ('rmw', 'tas'):
        addr = MEMBASE + (0x40 if kind == 'rmw' else 0x20)
        if 'mem' in w:
            r = st.mem[addr]
            fexp |= (1 << 5) | (1 << 6)
    elif rn in w:
        r = st.reg[rn]
        fexp |= 1 << 5
    if 't' in w:
        fexp |= 1 << 3
    if 'qm' in w:
        fexp |= 1 << 4
    tq = st.reg['qm']
    fexp |= st.reg['t'] | ((tq & 1) << 1) | (((tq >> 1) & 1) << 2)
    return struct.pack('<HBBIIIB3x', form.idx, ALU_OPS.index(op), t | (q << 1) | (m << 2), a & M32, b & M32,
                       r & M32, fexp)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'v1_alu')
    os.makedirs(out, exist_ok=True)
    rng = random.Random(0x5117091)
    names = [nm for nm, _, _ in ALU_FORMS] + [nm for nm, _ in MUL_FORMS]
    for i, nm in enumerate(names):
        isa.BYNAME[nm].idx = i
    missing = set(ALU_OPS) - {op for _, op, _ in ALU_FORMS}
    # every ALU op must be reachable from some isa.py form (A_SHAL/A_SHLL are distinct forms with one semantics)
    assert not missing, missing

    alu, per_form = [], {}
    for nm, op, kind in ALU_FORMS:
        form = isa.BYNAME[nm]
        recs = []

        def tqm():
            return rng.randrange(2), rng.randrange(2), rng.randrange(2)

        if kind == 'rr':
            if op in ('A_SHAD', 'A_SHLD'):
                for sgn in range(2):
                    for amt in range(32):
                        for k in range(128):
                            a = (sgn << 31) | amt | ((rng.getrandbits(26) << 5) if k & 1 else 0)
                            b = EDGES[k >> 1] if (k >> 1) < len(EDGES) else rnd32(rng)
                            recs.append(run_alu(rng, form, op, kind, a, b, *tqm()))
            elif op == 'A_CMPSTR':
                for mask in range(16):
                    for _ in range(1000):
                        a, d = rng.getrandbits(32), 0
                        for byte in range(4):
                            if not (mask >> byte) & 1:   # this byte differs: random nonzero, often one bit
                                d |= ((1 << rng.randrange(8)) if rng.randrange(2) else rng.randrange(1, 256)) << (8 * byte)
                        recs.append(run_alu(rng, form, op, kind, a, a ^ d, *tqm()))
            for a in EDGES:
                for b in EDGES:
                    if op in FLAG_READERS:
                        for f3 in range(8):
                            recs.append(run_alu(rng, form, op, kind, a, b, f3 & 1, (f3 >> 1) & 1, f3 >> 2))
                    else:
                        recs.append(run_alu(rng, form, op, kind, a, b, *tqm()))
            for _ in range(20000):
                a = rnd32(rng)
                b = rnd32(rng) if rng.randrange(8) else (a + rng.randrange(-2, 3)) & M32
                recs.append(run_alu(rng, form, op, kind, a, b, *tqm()))
        elif kind in ('rn', 'mrn', 'none'):
            for v in EDGES:
                for f3 in range(8):
                    g = rng.getrandbits(32)
                    a, b = (g, v) if kind == 'rn' else (v, g)
                    recs.append(run_alu(rng, form, op, kind, a, b, f3 & 1, (f3 >> 1) & 1, f3 >> 2))
            for _ in range(20000):
                a, b = rnd32(rng), rnd32(rng)
                recs.append(run_alu(rng, form, op, kind, a, b, *tqm()))
        elif kind in ('isn', 'isr0', 'izr0'):
            for imm in range(256):
                for k in range(64):
                    b = EDGES[k] if k < len(EDGES) else rnd32(rng)
                    if kind == 'isr0' and k == 63:
                        b = isa.sx8(imm) & M32          # make CMP/EQ #imm,R0 hit for every immediate
                    recs.append(run_alu(rng, form, op, kind, rng.getrandbits(32), b, *tqm(), imm=imm))
        elif kind == 'rmw':
            for imm in range(256):
                for byte in range(256):
                    recs.append(run_alu(rng, form, op, kind, 0, 0, *tqm(), imm=imm, byte=byte))
        elif kind == 'tas':
            for byte in range(256):
                for _ in range(4):
                    recs.append(run_alu(rng, form, op, kind, rng.getrandbits(32), 0, *tqm(), byte=byte))
        per_form[nm] = len(recs)
        alu += recs

    mul = []
    for nm, op in MUL_FORMS:
        form = isa.BYNAME[nm]
        pairs = [(a, b) for a in EDGES for b in EDGES] + [(rnd32(rng), rnd32(rng)) for _ in range(20000)]
        for a, b in pairs:
            st = base_state(rng)
            st.reg['r1'], st.reg['r2'] = b, a
            acc = (st.reg['mach'] << 32) | st.reg['macl']
            st.reg['s'] = s = rng.randrange(2)
            isa.execute(st, form, {'n': 1, 'm': 2}, PC)
            res = (st.reg['mach'] << 32) | st.reg['macl']
            mul.append(struct.pack('<HBBIIQQ', form.idx, MUL_OPS.index(op), s, a, b, acc, res))
        per_form[nm] = len(pairs)

    with open(os.path.join(out, 'vectors.bin'), 'wb') as fo:
        fo.write(b'V1AL' + struct.pack('<I', len(names)))
        for nm in names:
            fo.write(struct.pack('<B', len(nm)) + nm.encode())
        fo.write(struct.pack('<I', len(alu)) + b''.join(alu))
        fo.write(struct.pack('<I', len(mul)) + b''.join(mul))
    with open(os.path.join(out, 'v1_alu_ops.h'), 'w') as fo:
        fo.write('// generated by tb/v1_alu_tb.py from rtl/v1/v1_alu.sv and rtl/v1/v1_mul.sv -- do not edit\n#pragma once\n')
        fo.write('enum AluOp {' + ', '.join(f'{n} = {i}' for i, n in enumerate(ALU_OPS)) + f', A_NUM = {len(ALU_OPS)} }};\n')
        fo.write('static const char* ALU_OP_NAME[] = {' + ', '.join(f'"{n}"' for n in ALU_OPS) + '};\n')
        fo.write('enum MulOp {' + ', '.join(f'{n} = {i}' for i, n in enumerate(MUL_OPS)) + f', M_NUM = {len(MUL_OPS)} }};\n')
        fo.write('static const char* MUL_OP_NAME[] = {' + ', '.join(f'"{n}"' for n in MUL_OPS) + '};\n')
    print(f'v1_alu_tb.py: {len(names)} isa.py forms, {len(alu)} ALU + {len(mul)} MUL vectors -> {out}')


if __name__ == '__main__':
    main()
