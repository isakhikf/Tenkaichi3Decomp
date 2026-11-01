/*
 * UDE cycle-2 tests. LEVEL: unit + logic simulation on the host. Nothing here runs the game.
 *
 * The model below copies the PROVEN rules (C of the decompilation) the new responses rely on:
 *   - BtlMove_UpdateDefenseTimers (src/battle/btl_char_move.c): windows +0x1068 (evade), +0x106C (blast), +0x1078 (throw)
 *   - BtlHit_CheckDodge (btl_char_hit.c): first hit, attack flag 1, animation 0xB1, no behind test
 *   - BtlHit_CheckThrowBreak: attack 0x55, animation 0x31, facing the attacker, window +0x1078
 *   - the blast side step in btl_char_coll_b.c: window +0x106C, animation 0xB1, before the facing test
 *   - guard: fails from behind (BtlHit_CheckGuard); does not stop a throw
 * and the frame order of src/battle/btl_char_mgr.c (input sampling, then timers, then hits). What is NOT modelled:
 * animation data (which motions carry 0xB1 / 0x31), real contact timing, hit-stop, guard start-up.
 *
 * Usage: test_ude_v2 [edge_cover.txt] [db_scenarios.txt]   CSV on stdout as test_ude.c; exit code = failures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ude.h"
#define UDE_ACTION_COUNT_TEST 0x13C

static int gFail, gPass;
static void emit(const char *suite, const char *name, int ok, int dmg, const char *resp, int rf, const char *err0, const char *cause) {
    char err[256]; int i;
    for (i = 0; err0[i] && i < 255; i++) err[i] = err0[i] == ',' ? ';' : err0[i];
    err[i] = 0;
    printf("%s,logic_sim,%s,0,-,-,%s,%d,%s,%d,-,%s,%s\n", suite, name, resp, rf, ok ? "PASS" : "FAIL", dmg, err, cause);
    if (ok) gPass++; else gFail++;
}

enum { A_NEUTRAL = 0x0B, A_GUARD = 0x36, A_GUARD_HIT = 0x38, A_ATTACK = 0x70, A_REACT = 0xB9, A_STEP = 0x21, A_TECH = 0x105 };
enum { K_NONE, K_MELEE, K_THROW, K_BLAST };

typedef struct F2 {
    int opp_action, opp_frame, close, behind, attack_id, attack_flags, contact, kind, dmg, blast_eta10;
    int request_pending, flag5;
    unsigned physical;
    int disable;
} F2;
typedef struct R2 { int damage, evaded, blocked, broken, injected, taps, edges, max_tap_run, bad_edge, manual_lost, cleared, hits; } R2;

static F2 T[600];
static int TN;

static R2 run2(UdeConfig *c, int w0start, float hp) {
    UdeState st; UdeSnapshot s; UdeDecision d; R2 r; int i, my = A_NEUTRAL, w0 = w0start, w1 = -30, w4 = -30, run = 0;
    memset(&r, 0, sizeof r);
    Ude_Reset(&st);
    for (i = 0; i < TN; i++) {
        unsigned out; int edge, on;
        memset(&s, 0, sizeof s);
        s.valid = 1; s.fighting = 1; s.my_action = my; s.opp_action = T[i].opp_action; s.close = T[i].close;
        s.behind = T[i].behind; s.opp_attacking = T[i].kind == K_MELEE || T[i].kind == K_THROW; s.opp_skill_kind = -1;
        s.incoming_blast = T[i].blast_eta10 >= 0 && T[i].blast_eta10 <= 50; s.my_ki = 50000;
        s.have_ext = 1; s.anim_flags = 0xB1 | 0x31; s.opp_attack_id = T[i].attack_id; s.opp_attack_flags = T[i].attack_flags;
        s.flag5 = T[i].flag5; s.my_request_pending = T[i].request_pending; s.opp_action_frame = T[i].opp_frame;
        s.blast_eta10 = T[i].blast_eta10; s.win[0] = w0; s.win[1] = w1; s.win[4] = w4; s.win[2] = s.win[3] = s.win[5] = -30;
        c->enabled = !T[i].disable;
        out = Ude_Step(&st, c, &s, T[i].physical, &d);
        if ((out & T[i].physical) != T[i].physical || (out & ~T[i].physical & ~UDE_B_GUARD)) r.cleared++;
        if (d.injected && (T[i].physical & ~UDE_B_DIR_MASK)) r.manual_lost++;
        if (d.injected) r.injected++;
        if (d.response == UDE_RESP_TAP && d.injected) { r.taps++; run++; } else run = 0;
        if (run > r.max_tap_run) r.max_tap_run = run;
        edge = d.press_edge;
        if (edge) { r.edges++; if (d.response == UDE_RESP_TAP && (w0 > -30 && T[i].kind != K_THROW && T[i].kind != K_BLAST)) r.bad_edge++; }
        /* the game, this frame: timers (UpdateDefenseTimers), then the hit test */
        on = edge && T[i].flag5;
        w0--; if (w0 < -30) { w0 = -30; if (on) w0 = hp < 0.3f ? 2 : hp < 0.7f ? 3 : 4; } else if (on) w0 = -1;
        w1--; if (w1 < -30) { w1 = -30; if (on) w1 = 2; } else if (on) w1 = -1;
        w4--; if (w4 < -30) { w4 = -30; if (on) w4 = 5; }
        if (T[i].contact) {
            r.hits++;
            if (T[i].kind == K_THROW) {
                if (w4 > 0 && !T[i].behind) { r.broken++; my = A_NEUTRAL; } else { r.damage += T[i].dmg; my = A_REACT; }
            } else if (T[i].kind == K_BLAST) {
                if (w1 > 0) { r.evaded++; my = A_STEP; }
                else if (my >= 0x36 && my <= 0x46 && !T[i].behind) { r.blocked++; my = A_GUARD_HIT; }
                else { r.damage += T[i].dmg; my = A_REACT; }
            } else {
                if (w0 > 0 && (T[i].attack_flags & 1)) { r.evaded++; my = A_STEP; }
                else if (my >= 0x36 && my <= 0x46 && !T[i].behind) { r.blocked++; my = A_GUARD_HIT; }
                else { r.damage += T[i].dmg; my = A_REACT; }
            }
            continue;
        }
        if (my == A_REACT || my == A_STEP) { my = A_NEUTRAL; continue; }
        my = (out & UDE_B_GUARD) ? A_GUARD : A_NEUTRAL;
    }
    return r;
}

static void t_clear(void) { memset(T, 0, sizeof T); TN = 0; }
static void t_idle(int k) { while (k--) { T[TN].opp_action = A_NEUTRAL; T[TN].attack_id = -1; T[TN].blast_eta10 = -1; T[TN].flag5 = 1; TN++; } }
static void t_melee(int action, int contact_at, int behind, int flags, int dmg) {
    int k;
    for (k = 0; k <= contact_at; k++) {
        F2 *f = &T[TN++];
        f->opp_action = action; f->opp_frame = k; f->close = 1; f->behind = behind; f->attack_id = 3; f->attack_flags = flags;
        f->kind = K_MELEE; f->blast_eta10 = -1; f->flag5 = 1;
        if (k == contact_at) { f->contact = 1; f->dmg = dmg; }
    }
}
static void t_throw(int contact_at, int behind) {
    int k;
    for (k = 0; k <= contact_at; k++) {
        F2 *f = &T[TN++];
        f->opp_action = A_ATTACK + 0x20; f->opp_frame = k; f->close = 1; f->behind = behind; f->attack_id = 0x55; f->kind = K_THROW;
        f->blast_eta10 = -1; f->flag5 = 1;
        if (k == contact_at) { f->contact = 1; f->dmg = 1200; }
    }
}
static void t_blast(int frames, int behind) {
    int k;
    for (k = 0; k < frames; k++) {
        F2 *f = &T[TN++];
        f->opp_action = A_NEUTRAL; f->attack_id = -1; f->behind = behind; f->kind = K_BLAST; f->flag5 = 1;
        f->blast_eta10 = (frames - 1 - k) * 10;
        if (k == frames - 1) { f->contact = 1; f->dmg = 300; }
    }
}
static UdeConfig cfg(void) { UdeConfig c; Ude_DefaultConfig(&c); return c; }
static int common_ok(const R2 *r) { return r->cleared == 0 && r->manual_lost == 0 && r->max_tap_run <= 1 && r->bad_edge == 0; }
static void report(const char *name, const R2 *r, int ok, const char *cause) {
    char err[200];
    snprintf(err, sizeof err, "hits=%d evaded=%d blocked=%d broken=%d taps=%d edges=%d injected=%d cleared=%d max_tap=%d",
             r->hits, r->evaded, r->blocked, r->broken, r->taps, r->edges, r->injected, r->cleared, r->max_tap_run);
    emit("v2", name, ok && common_ok(r), r->damage, r->taps ? "tap" : r->injected ? "guard" : "none", 0, err, cause);
}

static void scenarios(void) {
    UdeConfig c; R2 r;
    /* behind, evadable, timing measured (6 frames): one tap, evaded */
    c = cfg(); Ude_ClearEta(); Ude_SetEta(A_ATTACK, 6);
    t_clear(); t_idle(5); t_melee(A_ATTACK, 6, 1, 1, 500); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_01_behind_evade_measured", &r, r.evaded == 1 && r.taps == 1 && r.damage == 0, "BtlHit_CheckDodge has no behind test");
    /* same, timing NOT measured: conservative, no input, damage (impossible to time) */
    Ude_ClearEta(); t_clear(); t_idle(5); t_melee(A_ATTACK, 6, 1, 1, 500); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_02_behind_unmeasured_conservative", &r, r.injected == 0 && r.damage == 500, "wait_timing: no measured contact time");
    /* behind, attack without flag 1: no defence exists */
    Ude_SetEta(A_ATTACK, 6); t_clear(); t_idle(5); t_melee(A_ATTACK, 6, 1, 0, 500); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_03_behind_not_evadable", &r, r.injected == 0 && r.damage == 500, "not_evadable: CheckDodge needs attack flag 1");
    /* throw from the front, measured: one tap, broken */
    Ude_SetEta(A_ATTACK + 0x20, 5); t_clear(); t_idle(5); t_throw(5, 0); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_04_throw_break_front", &r, r.broken == 1 && r.taps == 1 && r.damage == 0, "BtlHit_CheckThrowBreak window +0x1078");
    /* throw from behind: impossible */
    t_clear(); t_idle(5); t_throw(5, 1); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_05_throw_behind_impossible", &r, r.injected == 0 && r.damage == 1200, "break needs the defender facing");
    /* throw, unmeasured: v1 would hold guard (useless); v2 sends nothing */
    Ude_ClearEta(); t_clear(); t_idle(5); t_throw(5, 0); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_06_throw_unmeasured_no_useless_guard", &r, r.injected == 0, "guard does not stop a throw");
    /* blast from behind, timed from the game-side estimate: tap, side step */
    t_clear(); t_idle(5); t_blast(8, 1); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_07_blast_behind_dodge", &r, r.evaded == 1 && r.taps == 1 && r.damage == 0, "blast side step +0x106C before the facing test");
    /* blast from the front: v1 guard kept */
    t_clear(); t_idle(5); t_blast(8, 0); t_idle(5); r = run2(&c, -30, 1.0f);
    report("v2_08_blast_front_guard", &r, r.blocked + r.evaded == 1 && r.damage == 0 && r.taps == 0, "v1 behaviour preserved");
    /* window cooling: the evade window was just used (-5): a press would close it: no tap */
    Ude_SetEta(A_ATTACK, 6); t_clear(); t_melee(A_ATTACK, 6, 1, 1, 500); t_idle(3); r = run2(&c, -5, 1.0f);
    report("v2_09_window_cooling_no_press", &r, r.taps == 0, "press during cooldown sets the window to -1");
    /* two attacks from behind 12 frames apart: second within the anti-mash period: engine does not press again */
    t_clear(); t_idle(3); t_melee(A_ATTACK, 6, 1, 1, 500); t_idle(5); t_melee(A_ATTACK, 6, 1, 1, 500); t_idle(3); r = run2(&c, -30, 1.0f);
    report("v2_10_no_mashing", &r, r.taps == 1 && r.evaded == 1 && r.damage == 500, "second press would be refused by the game");
    /* flag 5 down: the press opens nothing: no tap */
    t_clear(); t_idle(3); t_melee(A_ATTACK, 6, 1, 1, 500); { int i; for (i = 0; i < TN; i++) T[i].flag5 = 0; } t_idle(3); r = run2(&c, -30, 1.0f);
    report("v2_11_no_flag5_no_tap", &r, r.taps == 0, "windows need fighter flag 5");
    /* player's request pending: no input over it */
    Ude_ClearEta(); t_clear(); t_idle(3); t_melee(A_ATTACK, 8, 0, 1, 500); { int i; for (i = 3; i < 7; i++) T[i].request_pending = 1; } t_idle(3);
    r = run2(&c, -30, 1.0f);
    { int ok = 1, i; UdeState st; UdeSnapshot s; UdeDecision d; Ude_Reset(&st);
      for (i = 0; i < 4; i++) { memset(&s, 0, sizeof s); s.valid = s.fighting = s.close = s.have_ext = 1; s.my_action = A_NEUTRAL;
          s.opp_action = A_ATTACK; s.my_request_pending = 1; s.opp_skill_kind = -1; s.my_ki = 1; Ude_Step(&st, &c, &s, 0, &d); ok = ok && !d.injected && d.reason == UDE_R_REQUEST_PENDING; }
      emit("v2", "v2_12_request_pending_respected", ok, 0, "none", 0, "-", "player's queued action never overridden"); }
    /* grace analysis: grace 2 + request check: after a RUSH tap the queued attack (request pending 2 frames) is safe */
    { UdeConfig g = cfg(); int i, ok = 1; UdeState st; UdeSnapshot s; UdeDecision d; g.manual_grace = 2; Ude_Reset(&st);
      for (i = 0; i < 8; i++) { unsigned phys = i == 0 ? 0x8 : 0; memset(&s, 0, sizeof s); s.valid = s.fighting = s.close = s.have_ext = 1;
          s.my_action = A_NEUTRAL; s.opp_action = A_ATTACK; s.opp_skill_kind = -1; s.my_ki = 1; s.my_request_pending = (i >= 1 && i <= 3);
          Ude_Step(&st, &g, &s, phys, &d); if (i <= 3 && d.injected) ok = 0; if (i == 5 && !d.injected) ok = 0; }
      emit("v2", "v2_13_grace2_with_request_check", ok, 0, "guard after request", 5, "-", "state-based arbitration covers the release frames"); }
    /* front timing on, measured: first press at the contact: the evade window is open: evaded instead of chip damage */
    { UdeConfig f = cfg(); f.front_timing = 1; Ude_SetEta(A_ATTACK, 6);
      t_clear(); t_idle(5); t_melee(A_ATTACK, 6, 0, 1, 500); t_idle(5); r = run2(&f, -30, 1.0f);
      report("v2_14_front_timing_evade", &r, r.evaded == 1 && r.damage == 0 && r.taps == 0, "press_hold: guard held after the timed press"); }
    /* toggle off during a planned tap: released at once, nothing sent */
    t_clear(); t_idle(3); t_melee(A_ATTACK, 6, 1, 1, 500); { int i; for (i = 0; i < TN; i++) T[i].disable = 1; } r = run2(&c, -30, 1.0f);
    c.enabled = 1;
    report("v2_15_toggle_off_no_tap", &r, r.injected == 0, "-");
    /* mixed sequence: front melee, throw, blast from behind, re-evaluated at each change */
    Ude_SetEta(A_ATTACK, 6); Ude_SetEta(A_ATTACK + 0x20, 5);
    t_clear(); t_idle(3); t_melee(A_ATTACK, 6, 0, 1, 500); t_idle(35); t_throw(5, 0); t_idle(35); t_blast(8, 1); t_idle(3);
    r = run2(&c, -30, 1.0f);
    report("v2_16_mixed_sequence", &r, r.damage == 0 && r.hits == 3, "guard, throw break, blast dodge");
    /* manual input during a planned tap: manual wins */
    t_clear(); t_idle(3); t_melee(A_ATTACK, 6, 1, 1, 500); { int i; for (i = 3; i < TN; i++) T[i].physical = 0x8; } r = run2(&c, -30, 1.0f);
    report("v2_17_manual_over_tap", &r, r.injected == 0 && r.manual_lost == 0, "player's attack kept");
    /* moving during a planned tap: movement not blocked */
    t_clear(); t_idle(3); t_melee(A_ATTACK, 6, 1, 1, 500); { int i; for (i = 0; i < TN; i++) T[i].physical = 0x40; } r = run2(&c, -30, 1.0f);
    report("v2_18_moving_over_tap", &r, r.injected == 0, "allow_while_moving=0");
    /* low health: window 2 frames, lead 1 still inside */
    t_clear(); t_idle(5); t_melee(A_ATTACK, 6, 1, 1, 500); t_idle(3); r = run2(&c, -30, 0.2f);
    report("v2_19_low_hp_short_window", &r, r.evaded == 1, "window 2 frames below 30% health");
    Ude_ClearEta();
}

/* ------------------------------------------------ property campaign over the extended snapshot */
static unsigned rng;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static void campaign(int runs) {
    int r, fails = 0; long frames = 0;
    static const int acts[] = { A_NEUTRAL, 0x0D, A_ATTACK, A_ATTACK + 5, A_TECH, 0x12D, 0xFD, 0xFA, 0x38, 0xB9 };
    for (r = 0; r < runs; r++) {
        UdeConfig c = cfg(); UdeState st; UdeSnapshot s; UdeDecision d; int i, n, tap_run = 0, bad = 0; const char *why = "";
        rng = 777u + (unsigned)r; n = 100 + (int)(rnd() % 400); Ude_Reset(&st); Ude_ClearEta();
        if (rnd() % 2) Ude_SetEta(A_ATTACK, 3 + (int)(rnd() % 10));
        c.front_timing = rnd() % 2; c.manual_grace = 2 + (int)(rnd() % 4);
        for (i = 0; i < n && !bad; i++) {
            unsigned phys = (rnd() % 4) ? 0 : (unsigned[]){ 0x1, 0x2, 0x4, 0x8, 0x40, 0x200, 0x4000 }[rnd() % 7], out; int k;
            memset(&s, 0, sizeof s);
            s.valid = rnd() % 100 != 0; s.online = rnd() % 200 == 0; s.replay = rnd() % 200 == 0; s.fighting = rnd() % 40 != 0;
            s.my_action = (rnd() % 3) ? A_NEUTRAL : acts[rnd() % 10]; s.opp_action = acts[rnd() % 10];
            s.close = rnd() % 2; s.behind = rnd() % 2; s.incoming_blast = rnd() % 5 == 0; s.opp_attacking = rnd() % 3 == 0;
            s.opp_skill_kind = (int)(rnd() % 4) - 1; s.my_ki = (int)(rnd() % 100000);
            s.have_ext = rnd() % 8 != 0; s.anim_flags = (rnd() % 4) ? 0xB1 : 0; s.opp_attack_id = (rnd() % 6) ? 3 : 0x55;
            s.opp_attack_flags = rnd() % 2; s.opp_hit_active = rnd() % 4 == 0; s.flag5 = rnd() % 5 != 0; s.no_evade = rnd() % 20 == 0;
            s.my_request_pending = rnd() % 10 == 0; s.opp_action_frame = (int)(rnd() % 12); s.blast_eta10 = (rnd() % 3) ? -1 : (int)(rnd() % 80);
            for (k = 0; k < UDE_W_COUNT; k++) s.win[k] = (rnd() % 3) ? -30 : (int)(rnd() % 35) - 30;
            c.enabled = rnd() % 60 != 0;
            out = Ude_Step(&st, &c, &s, phys, &d);
            if ((out & phys) != phys || (out & ~phys & ~UDE_B_GUARD)) { bad = 1; why = "bits"; }
            if (d.injected && (phys & ~(UDE_B_DIR_MASK | UDE_B_DERIVED))) { bad = 1; why = "manual"; }
            if (d.injected && (s.online || s.replay || !s.valid || !c.enabled || !s.fighting)) { bad = 1; why = "unsafe"; }
            if (d.injected && (phys & UDE_B_DIR_MASK)) { bad = 1; why = "moving"; }
            if (d.injected && s.have_ext && s.my_request_pending && d.press_edge) { bad = 1; why = "over request"; }
            tap_run = (d.injected && d.response == UDE_RESP_TAP) ? tap_run + 1 : 0;
            if (tap_run > 1) { bad = 1; why = "tap longer than one frame"; }
            if (d.press_edge && d.response == UDE_RESP_TAP && s.have_ext && !s.flag5) { bad = 1; why = "tap without flag 5"; }
        }
        frames += i;
        if (bad) { char e[96]; snprintf(e, sizeof e, "seed %u frame %d: %s", 777u + (unsigned)r, i - 1, why); emit("v2prop", "invariant", 0, 0, "-", i - 1, e, "regression needed"); fails++; }
    }
    { char e[96]; snprintf(e, sizeof e, "%d runs %ld frames %d failures", runs, frames, fails);
      emit("v2prop", "v2_invariants_campaign", fails == 0, 0, "-", 0, e, "-"); }
    Ude_ClearEta();
}

/* ------------------------------------------------ database-driven: edge-cover sequences and per-state expectations */
static void edge_cover(const char *path) {
    FILE *f = fopen(path, "r"); char line[4096]; int seqs = 0, frames = 0, bad = 0, states[UDE_ACTION_COUNT_TEST] = {0}, ns = 0, i;
    if (!f) { emit("db", "edge_cover_sequences", 0, 0, "-", 0, "file missing", path); return; }
    while (fgets(line, sizeof line, f)) {
        UdeConfig c = cfg(); UdeState st; UdeSnapshot s; UdeDecision d; int seq[64], m = 0, a, behind, j; char *tok;
        for (tok = strtok(line, " \n"); tok && m < 64; tok = strtok(NULL, " \n")) seq[m++] = (int)strtol(tok, NULL, 16);
        for (behind = 0; behind < 2; behind++) {
            Ude_Reset(&st);
            for (j = 0; j < m; j++) {
                a = seq[j];
                memset(&s, 0, sizeof s); s.valid = s.fighting = 1; s.close = 1; s.behind = behind; s.my_action = A_NEUTRAL;
                s.opp_action = a; s.opp_skill_kind = -1; s.my_ki = 50000; s.have_ext = 1; s.flag5 = 1; s.anim_flags = 0xB1;
                s.opp_attack_id = 3; s.opp_attack_flags = 1; s.blast_eta10 = -1; { int k; for (k = 0; k < UDE_W_COUNT; k++) s.win[k] = -30; }
                Ude_Step(&st, &c, &s, 0, &d);
                if (behind && d.injected && d.response == UDE_RESP_HOLD && d.threat != 0) bad++;   /* never guard from behind */
                if (d.injected && d.response == UDE_RESP_TAP && !behind) bad++;                    /* front threats: guard */
                if (a >= 0 && a < UDE_ACTION_COUNT_TEST && !states[a]) { states[a] = 1; ns++; }
                frames++;
            }
        }
        seqs++;
    }
    fclose(f);
    { char e[128]; snprintf(e, sizeof e, "%d sequences x front/behind, %d frames, %d opponent states driven, %d violations", seqs, frames, ns, bad);
      emit("db", "edge_cover_sequences", bad == 0 && seqs > 0, 0, "-", 0, e, "sequences from table combos (edge_cover)"); }
    (void)i;
}

static void db_states(const char *path) {
    FILE *f = fopen(path, "r"); char name[64]; int a, behind, expect, n = 0, bad = 0;
    if (!f) { emit("db", "state_expectations", 0, 0, "-", 0, "file missing", path); return; }
    while (fscanf(f, "%i %d %d %63s", &a, &behind, &expect, name) == 4) {
        UdeConfig c = cfg(); UdeState st; UdeSnapshot s; UdeDecision d; int k;
        Ude_Reset(&st); memset(&s, 0, sizeof s);
        s.valid = s.fighting = s.close = 1; s.behind = behind; s.my_action = A_NEUTRAL; s.opp_action = a; s.opp_skill_kind = 1;
        s.my_ki = 50000; s.have_ext = 1; s.flag5 = 1; s.anim_flags = 0xB1; s.opp_attack_id = 3; s.opp_attack_flags = 1; s.blast_eta10 = -1;
        for (k = 0; k < UDE_W_COUNT; k++) s.win[k] = -30;
        Ude_Step(&st, &c, &s, 0, &d);
        if (d.response != expect) { bad++; if (bad <= 5) fprintf(stderr, "state 0x%X behind %d: expected %d got %d (%s)\n", a, behind, expect, d.response, name); }
        n++;
    }
    fclose(f);
    { char e[128]; snprintf(e, sizeof e, "%d (state, orientation) cases from the database spec, %d mismatches", n, bad);
      emit("db", "state_expectations", bad == 0 && n > 0, 0, "-", 0, e, "spec: tools/validation/gen_scenarios.py"); }
}

int main(int argc, char **argv) {
    scenarios();
    campaign(2000);
    if (argc > 1) edge_cover(argv[1]);
    if (argc > 2) db_states(argv[2]);
    return gFail;
}
