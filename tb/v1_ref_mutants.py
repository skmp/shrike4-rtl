#!/usr/bin/env python3
# v1_ref_mutants.py -- planted bugs in WI-REF's files (tb/sh4_sys.h, tb/soc_checker.cpp, tb/sh4_iss.h), and in private
# copies of the owners' cache / BSC / P4-rule models for the facts the WI-REF parts pin through the system: each is
# built into a private copy of the testbench under --build and must make its test fail (`make v1-ref-mutants`).  The
# copies live under build/; the repository's files are never touched.  Each build / run streams its output to a log in
# the mutant's directory (only the verdict line is read back); at most --jobs (default 8, capped at 12) run at once.
#   usage: v1_ref_mutants.py [--build DIR] [--root DIR] [--only NAME,...] [--jobs N] [-v]
import os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

# (name, file, old text, new text, test): test = (binary, arguments) of the part that must fail
SYS, CHK, ISS = 'tb/sh4_sys.h', 'tb/soc_checker.cpp', 'tb/sh4_iss.h'
CAC, BSC, P4R = 'tb/sh4_cache.h', 'tb/sh4_bsc.h', 'tb/p4bus_ref.h'
T_SYS = lambda parts: ('sys', ['--only', parts])
T_ISS = lambda parts: ('iss', ['--only', parts])
T_BM = lambda prog: ('bm', [prog])
MUTANTS = [
    # the owners' cache / BSC models through the system testbench (private copies of tb/sh4_cache.h / tb/sh4_bsc.h: the
    # facts the WI-REF parts pin through Sh4Sys; the owners' own unit testbenches are theirs)
    ('ocbi_writes_back', CAC,
     'case DK_OCBI:\n            if (c && oc_hit(i, pa)) oc_v[i] = oc_u[i] = false;',
     'case DK_OCBI:\n            if (c && oc_hit(i, pa)) { if (oc_u[i]) wb(i); oc_v[i] = oc_u[i] = false; }', T_SYS('cache')),
    ('movca_dirty_keeps_data', CAC, 'if (oc_v[i] && oc_u[i]) { wb(i); memset(d, 0, 32); }', 'if (oc_v[i] && oc_u[i]) { wb(i); }',
     T_SYS('cache')),
    ('oc_assoc_no_writeback', CAC, 'if (c->oc_u[i] && (!((v >> 1) & 1) || !(v & 1))) c->wb(i);', '', T_SYS('cache')),
    ('oc_ram_index_bit7', CAC, 'return (int)((hi << 8) | 0x80 | ((va >> 5) & 0x7F));', 'return (int)((hi << 8) | ((va >> 5) & 0x7F));',
     T_SYS('cache')),
    ('oci_keeps_u', CAC, 'if (v & OCI) { memset(c->oc_v, 0, sizeof c->oc_v); memset(c->oc_u, 0, sizeof c->oc_u); }',
     'if (v & OCI) { memset(c->oc_v, 0, sizeof c->oc_v); }', T_SYS('cache')),
    ('ic_ignores_ice', CAC, 'if (!cacheable || !(ccr_ & ICE)) return (uint32_t)mem->read(pa & ~3u, 4);',
     'if (!cacheable) return (uint32_t)mem->read(pa & ~3u, 4);', T_SYS('cache')),
    ('movca_wt_allocates', CAC, 'if (!c || wt) {', 'if (!c) {', T_SYS('cache')),
    ('ocbp_clean_keeps_v', CAC, 'if (c && oc_hit(i, pa)) { if (oc_u[i]) wb(i); oc_v[i] = oc_u[i] = false; }',
     'if (c && oc_hit(i, pa) && oc_u[i]) { wb(i); oc_v[i] = oc_u[i] = false; }', T_SYS('cache')),
    ('wt_hit_sets_u', CAC, 'if (wt) mem->write(pa, size, wdata); else oc_u[i] = true;', 'if (wt) mem->write(pa, size, wdata); oc_u[i] = true;',
     T_SYS('cache')),
    ('pref_uncached_allocates', CAC, 'if (c && !oc_hit(i, pa)) fill(i, pa);', 'if (!oc_hit(i, pa)) fill(i, pa);', T_SYS('cache')),
    ('ic_assoc_no_tag_compare', CAC, 'else if (tr(true) && c->ic_v[i] && c->ic_tag[i] == tag_of(pa)) c->ic_v[i] = v & 1;',
     'else if (tr(true)) c->ic_v[i] = v & 1;', T_SYS('cache')),
    ('sq_ignores_qacr', CAC, '((((qacr_[n] >> 2) & 7) << 26) | (va & 0x03FFFFE0u))', '((3u << 26) | (va & 0x03FFFFE0u))', T_SYS('sq')),
    ('rfcr_key', BSC, 'krf = (w >> 10) == 0x29;', 'krf = (w >> 8) == 0xA4;', T_SYS('units')),
    # the VOLRD rule (SOC.md 6.3) as WI-P4 states it in tb/p4bus_ref.h, which the model applies: MMUCR and the flash
    ('p4rule_flash_not_volrd', P4R, 'return ((p >> 26) & 7) != 3 && p >= 0x00200000u;',
     'return ((p >> 26) & 7) != 3 && p >= 0x00220000u;', T_SYS('volrd,conform')),
    ('p4rule_mmucr_not_volrd', P4R, ' || (b == P4B_CCN_MMU && (a & 0xFF) == 0x10);', ';', T_SYS('volrd,conform')),
    # the system model (tb/sh4_sys.h)
    ('sq_at_not_synced', SYS, 'cache_model.c.mmu_at = mmu.at && mmu.translate;', 'cache_model.c.mmu_at = false;', T_SYS('sq')),
    ('volrd_every_uncached', SYS,
     'const bool logged = p4ref::lsu_volatile(kind, r.x.rg, va, r.x.pa, r.x.c);',
     'const bool logged = p4ref::lsu_volatile(kind, r.x.rg, va, r.x.pa, r.x.c) || !r.x.c;', T_SYS('volrd')),
    ('volrd_no_injection', SYS, 'if (vol) v = rv;', 'if (vol) v = model;', T_SYS('volrd')),
    ('volrd_nonvol_compare_off', SYS, 'else if (rv != (model & mask)) {', 'else if (false) {', T_SYS('volrd')),
    ('volrd_containment', SYS, ': (q.va == va && q.size == size);', ': (va >= q.va && va + size <= q.va + (uint32_t)q.size);',
     T_SYS('regress')),
    ('p4_quad_high_first', SYS,
     '                v = p4_read(va, r.p4a, 4, logged, 0);\n                v |= (uint64_t)p4_read(va + 4, r.p4a + 4, 4, logged, 1) << 32;',
     '                const uint64_t hi = p4_read(va + 4, r.p4a + 4, 4, logged, 1);\n                v = p4_read(va, r.p4a, 4, logged, 0) | (hi << 32);',
     T_SYS('volrd')),
    ('oce_not_synced', SYS, 'mmu.ccr.oce = c & sh4cache::OCE;', 'mmu.ccr.oce = true;', T_SYS('regress')),
    ('bsc_no_straps', SYS, 'if (por) { bsc.md = md; bsc.reset_pin();', 'if (por) { bsc.md = 0; bsc.reset_pin();', T_SYS('regress')),
    ('no_ideal_mem', SYS, 'cache = on ? static_cast<sh4sys::CacheX *>(&ideal) : static_cast<sh4sys::CacheX *>(&cache_model);',
     'cache = static_cast<sh4sys::CacheX *>(&cache_model);', T_SYS('regress')),
    ('ideal_sq_writes_memory', SYS, 'if (rg == sh4cache::RG_SQ || rg == sh4cache::RG_A7) return 0;', 'if (rg == sh4cache::RG_A7) return 0;',
     T_SYS('regress')),
    ('ldtlb_record_optional', SYS, 'if (ldtlb_idx < 0) {', 'if (false) {', T_SYS('regress')),
    ('ldtlb_urc_unchecked', SYS, "if (((uint32_t)ldtlb_idx & 63) != mmu.urc) {", 'if (false) {', T_SYS('regress,conform')),
    ('urc_counts_refill', SYS, 'mmu.use(itlb & 3);                                             // (URC',
     'mmu.use(itlb & 3); mmu.urc_step();                             // (URC', T_SYS('regress')),
    ('urc_first_access_not_counted', SYS, 'if (mmu_ovr ? mmu_window_counts(va, kind, x) : x.counted) mmu.urc_step();',
     '(void)x;', T_SYS('conform')),
    ('volrd_mmucr_fields_uncompared', SYS, 'if (vol && ((rv ^ model) & fixed)) {', 'if (false) {', T_SYS('conform')),
    ('volrd_mmucr_forced_nonvolatile', SYS, 'volmask = 0xFC000000u;', 'volmask = 0xFC000000u; vol = false;', T_SYS('conform')),
    ('itlb_self_refill_silent', SYS, 'if (lockstep) {                                               // the RTL refilled it',
     'if (false) {                                                 // the RTL refilled it', T_SYS('regress')),
    ('ic_unfilled_silent', SYS, 'if (lockstep && cache->ic_misses_unfilled() != unf) {', 'if (false) {', T_SYS('regress')),
    ('ic_no_victims', SYS, 'if (v.pa == (x.pa & 0x1FFFFFE0u)) { memcpy(&w, &v.d[va & 28], 4); found = true; }', '', T_SYS('regress')),
    ('ic_no_inval_window', SYS, 'if (!found && ic_inval_isn_ != ~0ull && isn - ic_inval_isn_ <= 16) {', 'if (false) {',
     T_SYS('regress')),
    ('itlb_no_victims', SYS, 'victim = victim || (itlb_victim_ok(v) && mmu.imatch(v.e, va, md_));', '(void)v;',
     T_SYS('regress')),
    ('irq_ignores_bl', SYS,
     'else if (nmi ? (bl && !cpu.asleep && !intc.nmib()) : (bl && !cpu.asleep))',
     'else if (nmi ? (bl && !cpu.asleep && !intc.nmib()) : false)', T_SYS('irq')),
    ('irq_level_off_by_one', SYS, 'else if (!nmi && lvl <= imask)', 'else if (!nmi && lvl < imask)', T_SYS('irq')),
    ('irq_imask_not_checked', SYS, 'else if (!nmi && lvl <= imask)', 'else if (!nmi && lvl <= 0)', T_SYS('conform')),
    ('irq_nmi_needs_nmib_even_asleep', SYS, 'else if (nmi ? (bl && !cpu.asleep && !intc.nmib())', 'else if (nmi ? (bl && !intc.nmib())',
     T_SYS('irq')),
    ('tra_mask', SYS, 'add(0x20, 4, 0x3FC, 0);', 'add(0x20, 4, 0x3FF, 0);', T_SYS('p4')),
    ('exc_regs_manual_resets_tra', SYS,
     'void reset_code(uint32_t code, bool por) { if (por) RegFile::reset(true); set(0x24, code & 0xFFF); }',
     'void reset_code(uint32_t code, bool por) { RegFile::reset(true); set(0x24, code & 0xFFF); }', T_SYS('p4')),
    ('entry_no_tea', SYS,
     '        exc.excw(e.kind == Sh4Entry::IRQ, e.code, e.tra_v, e.tra);\n        if (e.tea_v) mmu.exc_capture(e.tea);',
     '        exc.excw(e.kind == Sh4Entry::IRQ, e.code, e.tra_v, e.tra);', T_SYS('p4')),
    ('urc_counts_pref_drop', SYS,
     '        default: break;                                                 // P4 / arrays / reserved: no transaction [P]\n        }\n        if (r.x.counted) mmu.urc_step();',
     '        default: break;\n        }\n        mmu.urc_step();', T_SYS('mmu')),
    ('mstp_not_applied', SYS, 'sci.mstp = mstp & 1; scif.mstp = (mstp >> 3) & 1;', 'sci.mstp = false; scif.mstp = false;', T_SYS('units')),
    ('tuni0_tuni1_swapped', SYS, 'set(Sh4Intc::S_TUNI0, t & 1); set(Sh4Intc::S_TUNI1, (t >> 1) & 1);',
     'set(Sh4Intc::S_TUNI1, t & 1); set(Sh4Intc::S_TUNI0, (t >> 1) & 1);', T_SYS('irq')),
    ('ubc_events_lost', SYS,
     'if (!cpu.asleep) { pmc.count(0x19, ubc.n_ev_a - ev_a_seen_); pmc.count(0x1A, ubc.n_ev_b - ev_b_seen_); }', '',
     T_BM('ubcbrk')),
    # the checker
    ('chk_no_value_compare', CHK, 'if (((uint32_t)kv.second ^ e.after[a]) & mask) {', 'if (false) {', T_SYS('checker')),
    ('chk_no_missing_record', CHK,
     'if (!last.count(a) && !lenient) { fail(', 'if (false) { fail(', T_SYS('checker')),
    ('chk_no_memw_compare', CHK, 'if (rtl_b != mod_b) {', 'if (false) {', T_SYS('checker')),
    ('chk_irq_legality_ignored', CHK, 'if (!m.irq(code, why, take)) {', 'if (!m.irq(code, why, take) && false) {', T_SYS('checker')),
    # the INTC registers' landing window (SOC.md 6.2 / 3.1.5 / 6.3; M1a-verify F1): no allowance, an unbounded one, one
    # a cycle short, the take age ignored, the store not noted
    ('irq_no_landing_window', SYS, 'if (take_cycle > p.cycle + intc_window()) continue;', 'continue;', T_SYS('irq,conform')),
    ('irq_landing_window_open', SYS,
     ['if (take_cycle > p.cycle + intc_window()) continue;', 'intc_prev.pop_front();   // (past)'], ['', 'break;'],
     T_SYS('irq,conform')),
    ('irq_landing_window_short', SYS, 'return (uint64_t)(4 * P + 2 * B - 2);', 'return (uint64_t)(4 * P + 2 * B - 3);',
     T_SYS('irq,conform')),
    ('irq_landing_window_fixed', SYS, 'return (uint64_t)(4 * P + 2 * B - 2);', 'return 18;', T_SYS('irq')),
    ('chk_take_age_ignored', CHK, 'take = age <= cyc ? cyc - age : 0;', 'take = cyc;', T_SYS('conform')),
    ('chk_intc_store_not_noted', CHK, 'm.note_intc_store(ack, ib);', '', T_SYS('conform')),
    ('chk_intc_store_at_retire', CHK, 'if (x.kind == soc::CK_MEMW && x.cycle < ack) ack = x.cycle;', '', T_SYS('conform')),
    # the UBC settle windows (M1b, WI-SOC; SOC.md 6.2): conform (11)
    ('ubc_fwin_wide', SYS, 'static const int UBC_FWIN = 13, UBC_EWIN = 3;', 'static const int UBC_FWIN = 14, UBC_EWIN = 3;',
     T_SYS('conform')),
    ('ubc_fwin_narrow', SYS, 'static const int UBC_FWIN = 13, UBC_EWIN = 3;', 'static const int UBC_FWIN = 12, UBC_EWIN = 3;',
     T_SYS('conform')),
    ('ubc_ewin_wide', SYS, 'static const int UBC_FWIN = 13, UBC_EWIN = 3;', 'static const int UBC_FWIN = 13, UBC_EWIN = 4;',
     T_SYS('conform')),
    ('ubc_ewin_narrow', SYS, 'static const int UBC_FWIN = 13, UBC_EWIN = 3;', 'static const int UBC_FWIN = 13, UBC_EWIN = 2;',
     T_SYS('conform')),
    ('ubc_pre_hint_ignored', SYS, 'if (ubc_hint >= 0 && !((cpu.s.sr >> 28) & 1) && ubc_would(ubc, a, b) != want) {',
     'if (false) {', T_SYS('conform')),
    ('ubc_slot_distance', SYS, 'const int d = pc != cpu.s.pc ? 1 : 0;', 'const int d = 0;', T_SYS('conform')),
    ('ubc_no_tent', SYS, 'if (r && lockstep) ubc_build_tent();', '', T_SYS('conform')),
    ('ubc_tent_flags_kept', SYS, 'ubc = ubc_tent.alt_ubc;', '', T_SYS('conform')),
    # the MMUCR landing window (SOC.md 6.2, M2): tb/sh4_sys_tb.cpp conform (12)
    ('mmu_win_wide', SYS, 'static const int MMU_WIN = 3;', 'static const int MMU_WIN = 4;', T_SYS('conform')),
    ('mmu_win_narrow', SYS, 'static const int MMU_WIN = 3;', 'static const int MMU_WIN = 2;', T_SYS('conform')),
    ('mmu_pick_never', SYS, 'if (!mmu_prev.empty()) mmu_window_pick(va, kind, md_, rmw, x);', '', T_SYS('conform')),
    ('mmu_basis_kept', SYS, 'if (mmu_ovr) { if (mmucr_wr_gen == mw0) mmu_set_basis(mmu_cur_b); mmu_ovr = false; }',
     'if (mmu_ovr) { mmu_ovr = false; }', T_SYS('conform')),
    ('chk_mmucr_store_not_noted', CHK, 'if (!(m.mmu_basis() == mb)) { m.note_mmucr_store(n0, mb); n_mmucr_store++; }', '',
     T_SYS('conform')),
    ('chk_mmu_exc_hint_lost', CHK, 'm.mmu_hint = (int)code;', 'm.mmu_hint = -1;', T_SYS('conform')),
    # M2-fix (agents/reports/integration/M2-fix.md): tb/sh4_sys_tb.cpp conform (13) PTEH.ASID in the UBC's settle windows
    # [HW ptehubc], (14) URC inside MMUCR's landing window (SOC.md 3.8), (15) the ITLB victims' three rules
    ('ubc_basis_no_asid', SYS, 'b.asid = mmu.asid(); return b;', 'return b;', T_SYS('conform')),
    ('ubc_ftags_live_asid', SYS, 'bool &a, bool &b) {\n        const uint8_t asid = r.asid;',
     'bool &a, bool &b) {\n        const uint8_t asid = mmu.asid();', T_SYS('conform')),
    ('ubc_otags_live_asid', SYS, 'dbg::SZ_Q;\n        const uint8_t asid = r.asid;', 'dbg::SZ_Q;\n        const uint8_t asid = mmu.asid();',
     T_SYS('conform')),
    ('urc_window_uncounted', SYS, 'if (mmu_ovr) r.x.counted = mmu_window_counts(va, kind, r.x);', '', T_SYS('conform')),
    ('urc_window_basis_at', SYS, 'return mmu.translate && mmu_cur_b.at && xr', 'return mmu.translate && mmu.at && xr',
     T_SYS('conform')),
    ('itlb_victim_no_age', SYS, 'return !v.dead && isn - v.isn < FETCH_AHEAD;', 'return !v.dead;', T_SYS('conform')),
    ('itlb_victim_no_fillword', SYS, 'for (size_t j = 0; j <= i; j++) itlb_victims_[j].dead = true;', '',
     T_SYS('conform')),
    ('itlb_victim_no_hint', SYS, 'if (nv && mmu_hint == nv) { victim = false; n_itlb_victim_refused++; }', '',
     T_SYS('conform')),
    # M3-fix (agents/reports/integration/M3-fix.md): conform (16) the IFILL order around a landed store (M3-verify F4),
    # (17) untranslated area 7 through the model's OC with the BSC's phantom backing (M3-verify F5)
    ('chk_ifill_cycle_end', CHK, 'if (ifill_after_landed)\n        for (auto &e : early)', 'if (false)\n        for (auto &e : early)',
     T_SYS('conform')),
    ('a7_bypasses_cache', SYS, 'case mmuref::RG_SQ: case mmuref::RG_P4RSV:                       // SQ junk / reserved',
     'case mmuref::RG_SQ: case mmuref::RG_A7: case mmuref::RG_P4RSV:  // SQ junk / reserved', T_SYS('conform')),
    ('a7_store_dropped', SYS, 'case mmuref::RG_P4RSV: break;\n        case mmuref::RG_P4REG',
     'case mmuref::RG_A7: case mmuref::RG_P4RSV: break;\n        case mmuref::RG_P4REG', T_SYS('conform')),
    ('a7_backed_by_board', SYS, 'static bool phantom(uint32_t pa) { return ((pa >> 26) & 7) == 7; }',
     'static bool phantom(uint32_t pa) { (void)pa; return false; }', T_SYS('conform')),
    ('chk_ubc_store_not_noted', CHK, 'if (!(m.ubc_basis() == ub)) { m.note_ubc_store(n0, ub); n_ubc_store++; }', '',
     T_SYS('conform')),
    ('chk_ubc_resolve_never', CHK, 'if (adopt) { m.ubc_adopt_tent(); note("model: " + m.ubc_stale_note); }', '',
     T_SYS('conform')),
    ('chk_ubc_resolve_always', CHK, 'const bool adopt = (t.cur_brk && !rtl_brk && !t.alt_brk) || (!t.cur_brk && rtl_brk && t.alt_brk && !next_may_pre);',
     'const bool adopt = true;', T_SYS('conform')),
    ('chk_volrd_leftover_ignored', CHK, ['if (!m.volq.empty()) {\n        fail(what', 'if (!failed && !m.volq.empty()) fail('],
     ['if (false) {\n        fail(what', 'if (false) fail('], T_SYS('checker')),
    ('chk_exc_spc_ignored', CHK, 'if (!reset && m.cpu.s.spc != r.addr) {', 'if (false) {', T_SYS('checker')),
    ('chk_sr_masked', CHK, 'const uint32_t mask = a == A_FPSCR ? ~0x0003F07Cu : 0xFFFFFFFFu;',
     'const uint32_t mask = a == A_FPSCR ? ~0x0003F07Cu : a == A_SR ? ~0x303u : 0xFFFFFFFFu;', T_SYS('checker')),
    ('chk_fpscr_enables_masked', CHK, 'const uint32_t mask = a == A_FPSCR ? ~0x0003F07Cu : 0xFFFFFFFFu;',
     'const uint32_t mask = a == A_FPSCR ? ~0x0003FFFCu : 0xFFFFFFFFu;', T_SYS('checker')),
    ('chk_region_not_compared', CHK, 'if (mod_rg.count(kv.first) && mod_rg[kv.first] != kv.second) {', 'if (false) {',
     T_SYS('checker')),
    ('chk_gpr_other_bank_as_current', CHK, 'x.idx < 24 ? (int)(8 * (bank ^ 1) + x.idx - 16)', 'x.idx < 24 ? (int)(8 * bank + x.idx - 16)',
     T_SYS('checker')),
    ('chk_sleep_pc_ignored', CHK, 'if (!m.cpu.asleep || m.cpu.s.pc != x.addr + 2)', 'if (!m.cpu.asleep)', T_SYS('checker')),
    ('chk_sleep_order_strict', CHK, 'if (later) { held.push_back(x); continue; }', 'if (false) { held.push_back(x); continue; }',
     T_SYS('regress')),
    ('chk_dmaw_stamp_unchecked', CHK, 'if (y.kind == soc::CK_RETIRE && y.isn == x.isn) {', 'if (false) {', T_SYS('regress')),
    ('chk_no_dbrk_follower', CHK, 'const bool follower = m.data_break_pending();', 'const bool follower = false;', T_SYS('regress')),
    ('chk_undefined_kind_ignored', CHK, 'if (r.kind > soc::CK_ITLBFILL) {', 'if (false) {', T_SYS('regress')),
    ('chk_cycle_order_ignored', CHK, 'if (have_cycle && r.cycle < cbuf_cycle) {', 'if (false) {', T_SYS('regress')),
    ('chk_dbrk_slot_exc_accepted', CHK, 'if (!irq && m.data_break_pending() && m.cpu.pending_entry().dbrk_slot) {', 'if (false) {',
     T_SYS('conform')),
    ('chk_mhit_no_reset_needed', CHK, 'if (!failed && mhit_stop)\n        fail(', 'if (false)\n        fail(', T_SYS('regress')),
    ('chk_mhit_not_stopping', CHK, '    check_mhit(dis);\n}', '}', T_SYS('regress')),
    ('chk_arr_mhit_reset_unchecked', CHK, 'if (kind == RST_ARR_MHIT && !mhit_stop) {', 'if (false) {', T_SYS('regress')),
    ('chk_reset_strict_close', CHK, 'close(open.front(), true);', 'close(open.front(), false);', T_SYS('conform')),
    ('chk_reset_drops_open', CHK, 'while (!open.empty() && !failed) { close(open.front(), true); open.pop_front(); n_closed_at_reset++; }',
     'open.clear();', T_SYS('conform')),
    # the ISS (SoC mode)
    ('iss_fd_after_illegal', ISS,
     "    if ((s.sr & 0x8000u) && op != 0xFFFD && fpu_insn(f, op)) { e.code = in_slot ? EXP_SLOT_FPUDIS : EXP_FPU_DIS; return 1; }\n    if (in_slot && slot_illegal(f)) { e.code = EXP_SLOT_ILL; return 1; }\n    if (f == F_ILLEGAL) { e.code = EXP_ILLEGAL; return 1; }",
     "    if (in_slot && slot_illegal(f)) { e.code = EXP_SLOT_ILL; return 1; }\n    if (f == F_ILLEGAL) { e.code = EXP_ILLEGAL; return 1; }\n    if ((s.sr & 0x8000u) && op != 0xFFFD && fpu_insn(f, op)) { e.code = in_slot ? EXP_SLOT_FPUDIS : EXP_FPU_DIS; return 1; }",
     T_ISS('exc')),
    ('iss_slot_spc', ISS, 'e.spc = in_slot ? pc - 2 : pc;', 'e.spc = pc;', T_ISS('exc')),
    ('iss_mac_rm_first', ISS, 'const uint32_t vn = k == 4 ? rd32(an) : rd16(an), vm = k == 4 ? rd32(am) : rd16(am);',
     'const uint32_t vm = k == 4 ? rd32(am) : rd16(am), vn = k == 4 ? rd32(an) : rd16(an);', T_ISS('exc')),
    ('iss_bl_no_reset', ISS, 'const bool blr = e.kind == Sh4Entry::EXC && !e.ubrk && ((s.sr >> 28) & 1);',
     'const bool blr = false;', T_ISS('exc')),
    ('iss_fpu_host_in_soc', ISS, 'if (soc && fpu_kind(f) == 1) { fp_soc(f, op, true, nullptr); return true; }', '',
     T_SYS('fpdn')),
    ('iss_fpu_trap_vz_only', ISS, 'if (m.trap) { if (trap_fpscr) *trap_fpscr = m.fpscr; return true; }',
     'if (m.trap) { if (trap_fpscr) *trap_fpscr = (fs & ~0x0003F000u) | (m.fpscr & 0x00038000u) | (fs & 0x7Cu) | '
     '((m.fpscr & 0x00018000u) >> 10); return true; }', T_SYS('fpdn,conform')),
    ('iss_fsrra_e_needs_enable', ISS, 'if (!has_mem(f) && fk != 2) return 0;',
     'if (!has_mem(f) && !(fk == 2 && ((s.fpscr >> 7) & 0x1Fu))) return 0;', T_SYS('fpdn,conform')),
    ('iss_fdiv_d_operands_swapped', ISS, 'dop = D::FDIV_D;   dr(n, 0); dr(mm, 2);', 'dop = D::FDIV_D;   dr(mm, 0); dr(n, 2);',
     T_SYS('fpdn')),
    ('iss_fmac_addend_first', ISS, 'in[0] = FR(0); in[1] = FR(mm); in[2] = FR(n);', 'in[0] = FR(n); in[1] = FR(mm); in[2] = FR(0);',
     T_SYS('fpdn')),
    ('iss_pr_sz1_as_m1', ISS, 'want = (uint8_t)(pr ? (sz ? 6 : 4) : (sz ? 2 : 1));', 'want = (uint8_t)(pr ? (sz ? 2 : 4) : (sz ? 2 : 1));',
     T_SYS('conform')),
    ('iss_urc_acked_dropped', ISS, 'for (int k = 0; k < pr_.fxi && k < 6; k++) ackd_[nackd_++] = pr_.a[k];', '', T_SYS('conform')),
    ('iss_rmw_prot_read_type', ISS, 'pr_.wr = dk_write(kind) || (rmw_ && (fx == FX_TLB_PROT || fx == FX_IPW));',
     'pr_.wr = dk_write(kind);', T_SYS('tlb')),
    ('iss_pr_undone', ISS, "        s.ssr = s.sr; s.spc = e.spc; s.sgr = s.r[15];",
     "        s.ssr = s.sr; s.spc = e.spc; s.sgr = s.r[15];\n        if (e.slot) s.pr = 0;", T_ISS('exc')),
    ('iss_prebreak_over_mhit', ISS,
     '    if (ffx == FX_MHIT) { e.code = fx_expevt(ffx, false, true); e.tea_v = true; e.tea = pc; return 1; }\n', '',
     T_SYS('regress')),
    ('iss_dbrk_not_marked', ISS, 'pend_.dbrk = !abrk;', 'pend_.dbrk = false;', T_SYS('regress')),
    ('iss_dbrk_many_followers', ISS, 'd.dbrk = !d.dbrk_slot && one_cycle && slot_pending;', 'd.dbrk = true;', T_SYS('regress,conform')),
    ('iss_dbrk_spc_not_moved', ISS, '        d.spc = s.pc;                                                // after the slot', '        //',
     T_SYS('regress,conform')),
    ('iss_dbrk_no_slot', ISS, 'const bool one_cycle = f == F_BRA || f == F_BSR || f == F_BTS || f == F_BFS;', 'const bool one_cycle = false;',
     T_SYS('conform')),
    ('iss_dbrk_any_branch_pairs', ISS, 'const bool one_cycle = f == F_BRA || f == F_BSR || f == F_BTS || f == F_BFS;',
     'const bool one_cycle = delayed_branch(f);', T_SYS('conform')),
]

TB_FILES = ['sh4_sys.h', 'sh4_iss.h', 'sh4_fpdn.h', 'sh4_p4if.h', 'p4bus_ref.h', 'mmu_ref.h', 'sh4_intc.h', 'sh4_dbg.h',
            'sh4_timers.h', 'scif_model.h', 'dmac_ref.h', 'sh4_cache.h', 'sh4_bsc.h', 'soc_commit.h', 'soc_checker.h', 'soc_checker.cpp',
            'soc_loggen.h', 'sh4_asm.h', 'sh4_sys_tb.cpp', 'sh4_sys_bm.cpp', 'v1_iss_tb.cpp']
CXX = ['g++', '-std=c++17', '-O2', '-frounding-math', '-w']


def tail_verdict(path):
    """the first FAIL line of a log (else its last line), reading at most its last 64 KB"""
    with open(path, 'rb') as f:
        f.seek(0, 2)
        n = f.tell()
        f.seek(max(0, n - 65536))
        lines = f.read().decode(errors='replace').splitlines()
    return next((l.strip() for l in lines if 'FAIL' in l), lines[-1].strip() if lines else '')


def run(cmd, log, cwd=None):
    """run cmd with its output streamed to `log` (never held in memory); returns the exit status"""
    with open(log, 'wb') as f:
        return subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=cwd).returncode


def run_one(m, root, build, verbose):
    name, fn, old, new, (kind, args) = m
    d = os.path.join(build, name)
    tb = os.path.join(d, 'tb')
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(tb)
    os.symlink(os.path.join(root, 'model'), os.path.join(d, 'model'))
    os.symlink(os.path.join(root, 'tools'), os.path.join(d, 'tools'))            # tools/hw/fpdn: fpdn_vec.h
    for f in TB_FILES:
        shutil.copy(os.path.join(root, 'tb', f), tb)
    p = os.path.join(d, fn)
    s = open(p).read()
    olds, news = (old, new) if isinstance(old, list) else ([old], [new])
    for o, n in zip(olds, news):
        if s.count(o) != 1:
            return name, 'ERROR', f'the text to mutate occurs {s.count(o)} times in {fn}'
        s = s.replace(o, n)
    open(p, 'w').write(s)
    if kind == 'sys':
        exe, srcs = os.path.join(d, 'sys_tb'), [os.path.join(tb, 'sh4_sys_tb.cpp'), os.path.join(tb, 'soc_checker.cpp')]
        cmd = [exe, '--bm-dir', os.path.join(root, 'build'), '--fpdn-dir', os.path.join(root, 'tools', 'hw', 'fpdn')] + args
    elif kind == 'iss':
        exe, srcs = os.path.join(d, 'iss_tb'), [os.path.join(tb, 'v1_iss_tb.cpp')]
        cmd = [exe, '--dir', os.path.join(root, 'build', 'obj_v1_iss'), '--t2', os.path.join(root, 'model', 'timings2')] + args
    else:
        exe, srcs = os.path.join(d, 'bm'), [os.path.join(tb, 'sh4_sys_bm.cpp')]
        prog = args[0]
        log = os.path.join(d, prog + '.log')
        cmd = [exe, os.path.join(root, 'build', 'bm', 'debug-ubc-hudi', prog + '.elf'), '--out', log]
    blog, rlog = os.path.join(d, 'build.log'), os.path.join(d, 'run.log')
    if run(CXX + ['-I' + tb, '-o', exe] + srcs, blog):
        return name, 'ERROR', 'build failed: ' + tail_verdict(blog)
    failed = run(cmd, rlog, cwd=d) != 0
    if kind == 'bm' and not failed:
        failed = run(['python3', os.path.join(root, 'tb', 'bm', 'debug-ubc-hudi', 'check.py'), log,
                      os.path.join(root, 'tb', 'bm', 'debug-ubc-hudi', 'expected', args[0] + '.json'),
                      '--classes', 'functional', '--quiet'], os.path.join(d, 'check.log')) != 0
    return name, 'caught' if failed else 'MISSED', tail_verdict(rlog)


def main():
    a = sys.argv[1:]
    root = os.path.abspath(a[a.index('--root') + 1]) if '--root' in a else os.getcwd()
    build = os.path.abspath(a[a.index('--build') + 1]) if '--build' in a else os.path.join(root, 'build', 'v1_ref_mut')
    jobs = min(12, int(a[a.index('--jobs') + 1]) if '--jobs' in a else 8)       # the machine: 24 cores, 15 GB
    only = a[a.index('--only') + 1].split(',') if '--only' in a else None
    verbose = '-v' in a
    ms = [m for m in MUTANTS if not only or m[0] in only]
    if any(m[4][0] == 'bm' for m in ms) and not os.path.exists(os.path.join(root, 'build', 'bm', 'debug-ubc-hudi', 'ubcbrk.elf')):
        print('v1-ref-mutants: build/bm/debug-ubc-hudi/ubcbrk.elf missing (make v1-ref-bm-images): its mutant is skipped')
        ms = [m for m in ms if m[4][0] != 'bm']
    os.makedirs(build, exist_ok=True)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        res = list(ex.map(lambda m: run_one(m, root, build, verbose), ms))
    bad = 0
    for name, st, det in res:
        print(f'  {name:28s} {st:7s} {det[:110] if (verbose or st != "caught") else ""}')
        bad += st != 'caught'
    print(f'v1-ref-mutants: {len(res) - bad} of {len(res)} planted bugs caught')
    print('v1-ref-mutants: ' + ('PASS' if not bad else 'FAIL'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
