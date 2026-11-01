/*
 * UDE host tests. LEVEL: unit tests + logic simulation. Nothing here runs the game: the "fight" below is a small
 * model of the parts the engine reads (action ids, reach flag, incoming blast, guard acceptance), so a pass here
 * says the engine's logic does what it should, not that the game reacts as modelled. In-game tests are separate
 * (docs: IN_GAME_TEST_PLAN in README).
 *
 * Output: one CSV line per result on stdout:  suite,level,name,seed,threat,initial,response,resp_frame,result,damage,affected,errors,cause
 * Exit code: number of failures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ude.h"

static int gFail;
static void emit(const char *suite, const char *level, const char *name, unsigned seed, const char *threat, const char *init,
                 const char *resp, int rf, int ok, int dmg, const char *aff, const char *err, const char *cause) {
    printf("%s,%s,%s,%u,%s,%s,%s,%d,%s,%d,%s,%s,%s\n", suite, level, name, seed, threat, init, resp, rf, ok ? "PASS" : "FAIL",
           dmg, aff, err, cause);
    if (!ok) gFail++;
}

/* ------------------------------------------------------------------ model of a fight (logic simulation only) */
enum { A_NEUTRAL = 0x0B, A_MOVE = 0x0D, A_GUARD = 0x36, A_GUARD_HIT = 0x38, A_ATTACK = 0x70, A_REACT = 0xB9,
       A_TECH = 0x105, A_RUSH = 0x12D, A_SKILL = 0xFD, A_CLASH = 0xFA, A_DASH_ATK = 0x5E };

typedef struct Frame {          /* one frame of the opponent's script plus what the player physically presses */
    int opp_action, close, blast, behind, opp_attacking, skill_kind;
    unsigned physical;
    int hit;                    /* the opponent's attack connects on this frame (damage unless guarding from the front) */
    int dmg;
    int unguardable;            /* e.g. a throw: guard does not help */
    int online, replay, paused;
} Frame;

typedef struct Result { int damage, blocked, hits, injected, first_inj, manual_lost, cleared_bits, max_hold; } Result;

static Result run(const Frame *f, int n, UdeConfig *cfg, int my_start) {
    UdeState st; UdeSnapshot s; UdeDecision d; Result r; int i, my = my_start;
    memset(&r, 0, sizeof r); r.first_inj = -1;
    Ude_Reset(&st);
    for (i = 0; i < n; i++) {
        unsigned out;
        memset(&s, 0, sizeof s);
        s.valid = 1; s.fighting = !f[i].paused; s.online = f[i].online; s.replay = f[i].replay;
        s.my_action = my; s.opp_action = f[i].opp_action; s.close = f[i].close; s.incoming_blast = f[i].blast;
        s.behind = f[i].behind; s.opp_attacking = f[i].opp_attacking; s.opp_skill_kind = f[i].skill_kind ? f[i].skill_kind : -1;
        s.my_ki = 50000; s.my_hp = 30000;
        out = Ude_Step(&st, cfg, &s, f[i].physical, &d);
        if ((out & f[i].physical) != f[i].physical) r.cleared_bits++;
        if ((out & ~f[i].physical) & ~UDE_B_GUARD) r.cleared_bits++;      /* added something other than guard */
        if (d.injected) { r.injected++; if (r.first_inj < 0) r.first_inj = i; }
        if ((f[i].physical & ~(UDE_B_DIR_MASK)) && d.injected) r.manual_lost++;
        if (st.holding > r.max_hold) r.max_hold = st.holding;
        /* the game: the button word becomes the fighter's action next frame (BtlAct_Update switches on dispatch) */
        if (f[i].hit) {
            r.hits++;
            if (my >= 0x36 && my <= 0x46 && !f[i].behind && !f[i].unguardable) { r.blocked++; my = A_GUARD_HIT; }
            else { r.damage += f[i].dmg; my = A_REACT; }
            continue;
        }
        if (my == A_REACT) { my = A_NEUTRAL; continue; }
        if (out & UDE_B_GUARD) my = A_GUARD;
        else if (f[i].physical & 0x8) my = A_ATTACK;                     /* RUSH pressed: own attack */
        else if (f[i].physical & UDE_B_DIR_MASK) my = A_MOVE;
        else my = A_NEUTRAL;
    }
    return r;
}

/* scenario builder */
static Frame F[400];
static int N;
static void clear(void) { memset(F, 0, sizeof F); N = 0; }
static void idle(int k) { while (k--) { F[N].opp_action = A_NEUTRAL; N++; } }
static void attack(int action, int startup, int active, int close, int dmg) {
    int k;
    for (k = 0; k < startup + active; k++) {
        F[N].opp_action = action; F[N].close = close; F[N].opp_attacking = 1;
        if (k >= startup) { F[N].hit = 1; F[N].dmg = dmg; }
        N++;
    }
}

static UdeConfig def(void) { UdeConfig c; Ude_DefaultConfig(&c); return c; }

static void scenario(const char *name, const char *threat, int my0, int expect_block_all, int expect_inject, int dmg_max) {
    UdeConfig c = def();
    Result r = run(F, N, &c, my0);
    int ok = r.cleared_bits == 0 && r.manual_lost == 0 && r.max_hold <= c.hold_cap;
    char aff[64], err[96];
    if (expect_block_all) ok = ok && r.blocked == r.hits;
    if (expect_inject >= 0) ok = ok && ((r.injected > 0) == expect_inject);
    if (dmg_max >= 0) ok = ok && r.damage <= dmg_max;
    snprintf(aff, sizeof aff, "manual_lost=%d", r.manual_lost);
    snprintf(err, sizeof err, "cleared=%d hits=%d blocked=%d max_hold=%d", r.cleared_bits, r.hits, r.blocked, r.max_hold);
    emit("scenario", "logic_sim", name, 0, threat, my0 == A_NEUTRAL ? "neutral" : "other", r.injected ? "guard" : "none",
         r.first_inj, ok, r.damage, aff, err, ok ? "-" : "see errors");
}

static void scenarios(void) {
    int i;
    /* 1 */ clear(); idle(5); attack(A_ATTACK, 4, 2, 1, 500); idle(10); scenario("01_simple_physical", "melee", A_NEUTRAL, 1, 1, 0);
    /* 2 */ clear(); idle(5); for (i = 0; i < 6; i++) attack(A_ATTACK + i, 3, 2, 1, 400); idle(10); scenario("02_long_rush_combo", "melee", A_NEUTRAL, 1, 1, 0);
    /* 3 */ clear(); idle(5); attack(A_ATTACK, 3, 1, 1, 500); attack(A_ATTACK + 7, 2, 2, 1, 500); idle(5); scenario("03_direction_change", "melee", A_NEUTRAL, 1, 1, 0);
    /* 4: from behind guard cannot work (combat.md): the engine must not fabricate a defence */
    clear(); idle(5); attack(A_ATTACK, 4, 2, 1, 500); for (i = 5; i < N; i++) F[i].behind = 1; idle(5); scenario("04_attack_from_behind", "melee", A_NEUTRAL, 0, 0, -1);
    /* 5 */ clear(); idle(5); attack(0x90, 5, 3, 1, 600); idle(5); scenario("05_aerial_attack", "melee", A_NEUTRAL, 1, 1, 0);
    /* 6 charged: long startup */ clear(); idle(5); attack(0x80, 25, 2, 1, 1500); idle(5); scenario("06_charged_attack", "melee", A_NEUTRAL, 1, 1, 0);
    /* 7 single ki blast: no attack action, the projectile approaches */
    clear(); idle(5); for (i = 0; i < 6; i++) { F[N].opp_action = A_NEUTRAL; F[N].blast = 1; if (i == 5) { F[N].hit = 1; F[N].dmg = 300; } N++; } idle(5);
    scenario("07_single_ki_blast", "blast", A_NEUTRAL, 1, 1, 0);
    /* 8 consecutive blasts */
    clear(); idle(3); for (i = 0; i < 30; i++) { F[N].opp_action = A_NEUTRAL; F[N].blast = 1; if (i % 5 == 4) { F[N].hit = 1; F[N].dmg = 200; } N++; } idle(5);
    scenario("08_consecutive_blasts", "blast", A_NEUTRAL, 1, 1, 0);
    /* 9 blast while the opponent moves */
    clear(); idle(3); for (i = 0; i < 8; i++) { F[N].opp_action = A_MOVE; F[N].blast = i > 1; if (i == 7) { F[N].hit = 1; F[N].dmg = 300; } N++; } idle(5);
    scenario("09_blast_during_movement", "blast", A_NEUTRAL, 1, 1, 0);
    /* 10 super / ultimate */
    clear(); idle(3); for (i = 0; i < 40; i++) { F[N].opp_action = A_TECH; F[N].skill_kind = 1; F[N].blast = i > 20; if (i == 35) { F[N].hit = 1; F[N].dmg = 4000; } N++; } idle(5);
    scenario("10_super_ultimate", "technique", A_NEUTRAL, 1, 1, 0);
    /* 11 throw: guard does not help; must not count as a defence */
    clear(); idle(3); attack(A_ATTACK + 0x20, 4, 1, 1, 1200); F[N - 1].unguardable = 1; idle(5);
    { UdeConfig c = def(); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "11_grab_throw", 0, "melee(throw)", "neutral", r.injected ? "guard" : "none", r.first_inj,
           r.cleared_bits == 0 && r.damage == 1200, r.damage, "-", "throw not defendable by guard (expected damage)", "known limitation"); }
    /* 12 counter: the opponent's counter is a melee action after the player's own attack */
    clear(); idle(3); for (i = 0; i < 3; i++) { F[N].opp_action = A_NEUTRAL; F[N].physical = 0x8; N++; } idle(4); attack(A_ATTACK + 3, 3, 2, 1, 700); idle(5);
    scenario("12_counter_attack", "melee", A_NEUTRAL, 1, 1, 0);
    /* 13 counter-counter: two exchanges */
    clear(); idle(3); attack(A_ATTACK, 3, 1, 1, 500); idle(6); attack(A_ATTACK + 1, 3, 1, 1, 500); idle(5); scenario("13_counter_counter", "melee", A_NEUTRAL, 1, 1, 0);
    /* 14 attack then cancel into a technique */
    clear(); idle(3); attack(A_ATTACK, 3, 1, 1, 500); for (i = 0; i < 20; i++) { F[N].opp_action = A_TECH; F[N].skill_kind = 2; if (i == 15) { F[N].hit = 1; F[N].dmg = 2000; } N++; } idle(3);
    scenario("14_attack_then_cancel", "melee+technique", A_NEUTRAL, 1, 1, 0);
    /* 15 distances: far attack action is not a threat (no reach) */
    clear(); idle(3); attack(A_ATTACK, 6, 0, 0, 0); idle(3); attack(A_ATTACK, 4, 1, 1, 500); idle(3); scenario("15_distances", "melee", A_NEUTRAL, 1, 1, 0);
    { UdeConfig c = def(); clear(); idle(3); attack(A_ATTACK, 8, 0, 0, 0); idle(3); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "15b_far_attack_no_input", 0, "melee(far)", "neutral", r.injected ? "guard" : "none", r.first_inj, r.injected == 0, 0, "-", "-", "-"); }
    /* 16 altitude changes: aerial attack ids while the player is in an airborne state that is not free */
    clear(); idle(3); attack(0x95, 4, 2, 1, 500); idle(3); scenario("16_altitude_changes", "melee", A_NEUTRAL, 1, 1, 0);
    /* 17 threats in short succession */
    clear(); idle(2); for (i = 0; i < 5; i++) { attack(A_ATTACK, 2, 1, 1, 300); idle(1); } idle(4); scenario("17_short_intervals", "melee", A_NEUTRAL, 1, 1, 0);
    /* 18 insufficient resources: min_ki above the player's ki: no guard, damage taken, reason no_ki */
    { UdeConfig c = def(); c.min_ki = 90000; clear(); idle(3); attack(A_ATTACK, 4, 1, 1, 500); idle(3); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "18_insufficient_ki", 0, "melee", "neutral", r.injected ? "guard" : "none", r.first_inj, r.injected == 0 && r.damage == 500, r.damage, "-", "-", "-"); }
    /* 19 manual input at the same time: the player presses RUSH during the threat: no guard, manual kept */
    clear(); idle(3); attack(A_ATTACK, 6, 1, 1, 500); for (i = 3; i < 9; i++) F[i].physical = 0x8; idle(3);
    { UdeConfig c = def(); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "19_manual_simultaneous", 0, "melee", "neutral", r.injected ? "guard" : "none", r.first_inj,
           r.manual_lost == 0 && r.cleared_bits == 0, r.damage, "manual kept", "-", "player's attack has priority (damage is the player's trade)"); }
    /* 19b moving: direction held: no guard (movement never blocked) */
    clear(); idle(3); attack(A_ATTACK, 6, 1, 1, 500); for (i = 0; i < N; i++) F[i].physical = 0x40; idle(3);
    { UdeConfig c = def(); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "19b_moving_not_blocked", 0, "melee", "moving", r.injected ? "guard" : "none", r.first_inj, r.injected == 0, r.damage, "-", "-", "allow_while_moving=0"); }
    /* 20 pause, scene transition, online, replay */
    clear(); idle(2); attack(A_ATTACK, 6, 1, 1, 500); for (i = 0; i < N; i++) F[i].paused = 1;
    { UdeConfig c = def(); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "20a_pause_scene", 0, "melee", "paused", r.injected ? "guard" : "none", r.first_inj, r.injected == 0, r.damage, "-", "-", "-"); }
    for (i = 0; i < N; i++) { F[i].paused = 0; F[i].online = 1; }
    { UdeConfig c = def(); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "20b_online_disabled", 0, "melee", "online", r.injected ? "guard" : "none", r.first_inj, r.injected == 0, r.damage, "-", "-", "-"); }
    for (i = 0; i < N; i++) { F[i].online = 0; F[i].replay = 1; }
    { UdeConfig c = def(); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "20c_replay_disabled", 0, "melee", "replay", r.injected ? "guard" : "none", r.first_inj, r.injected == 0, r.damage, "-", "-", "-"); }
    /* clash and skill: never touched */
    clear(); for (i = 0; i < 30; i++) { F[N].opp_action = A_CLASH; F[N].close = 1; N++; }
    { UdeConfig c = def(); Result r = run(F, N, &c, A_NEUTRAL);
      emit("scenario", "logic_sim", "21_clash_untouched", 0, "clash", "neutral", r.injected ? "guard" : "none", r.first_inj, r.injected == 0, 0, "-", "-", "-"); }
}

/* ------------------------------------------------------------------ unit tests */
static void units(void) {
    UdeConfig c = def(); UdeState s; UdeSnapshot n; UdeDecision d; int i, ok;
    memset(&n, 0, sizeof n); n.valid = 1; n.fighting = 1; n.my_action = A_NEUTRAL; n.opp_action = A_ATTACK; n.close = 1; n.my_ki = 1;
    n.opp_skill_kind = -1;
    Ude_Reset(&s);
    Ude_Step(&s, &c, &n, 0, &d);
    emit("unit", "unit", "inject_on_close_attack", 0, "melee", "neutral", "guard", 0, d.injected == 1 && d.buttons == UDE_B_GUARD, 0, "-", "-", "-");
    /* toggle off mid-guard: released at once */
    c.enabled = 0; Ude_Step(&s, &c, &n, 0, &d);
    emit("unit", "unit", "toggle_off_releases", 0, "melee", "guarding", "none", 1, d.injected == 0 && s.mode == UDE_OFF, 0, "-", "-", "-");
    c.enabled = 1; Ude_Step(&s, &c, &n, 0, &d);
    emit("unit", "unit", "toggle_on_resumes", 0, "melee", "neutral", "guard", 2, d.injected == 1 && s.mode == UDE_ON, 0, "-", "-", "-");
    /* manual grace */
    Ude_Reset(&s); Ude_Step(&s, &c, &n, 0x2, &d); ok = d.injected == 0 && d.buttons == 0x2;
    for (i = 1; i < c.manual_grace; i++) { Ude_Step(&s, &c, &n, 0, &d); ok = ok && d.injected == 0; }
    Ude_Step(&s, &c, &n, 0, &d); ok = ok && d.injected == 1;
    emit("unit", "unit", "manual_grace_window", 0, "melee", "neutral", "guard after grace", c.manual_grace, ok, 0, "DASH kept", "-", "-");
    /* hold cap then cooldown */
    Ude_Reset(&s); n.my_action = A_GUARD; ok = 1;
    for (i = 0; i < c.hold_cap; i++) { Ude_Step(&s, &c, &n, 0, &d); ok = ok && d.injected; }
    Ude_Step(&s, &c, &n, 0, &d); ok = ok && !d.injected && d.reason == UDE_R_HOLD_CAP;
    for (i = 0; i < c.cooldown; i++) { Ude_Step(&s, &c, &n, 0, &d); ok = ok && !d.injected; }
    Ude_Step(&s, &c, &n, 0, &d); ok = ok && d.injected;
    emit("unit", "unit", "hold_cap_and_cooldown", 0, "melee", "guard", "release", c.hold_cap, ok, 0, "-", "-", "-");
    /* not accepted twice -> suspension */
    Ude_Reset(&s); n.my_action = A_NEUTRAL; ok = 1;
    for (i = 0; i < 400 && s.stats.suspends == 0; i++) Ude_Step(&s, &c, &n, 0, &d);
    ok = s.stats.suspends == 1 && s.mode == UDE_SUSPENDED;
    emit("unit", "unit", "refused_guard_suspends", 0, "melee", "neutral(no guard accepted)", "suspend", i, ok, 0, "-", "-", "-");
    /* bad action id */
    Ude_Reset(&s); n.opp_action = 5000; Ude_Step(&s, &c, &n, 0, &d);
    emit("unit", "unit", "bad_snapshot_suspends", 0, "-", "-", "none", 0, d.injected == 0 && s.mode == UDE_SUSPENDED, 0, "-", "-", "-");
    /* busy: player attacking */
    Ude_Reset(&s); n.opp_action = A_ATTACK; n.my_action = A_ATTACK; Ude_Step(&s, &c, &n, 0, &d);
    emit("unit", "unit", "busy_player_attack_untouched", 0, "melee", "attacking", "none", 0, d.injected == 0 && d.reason == UDE_R_BUSY, 0, "-", "-", "-");
    /* policy table sanity: ranges from the database */
    ok = Ude_IsFreeAction(0xB) && !Ude_IsFreeAction(0x70) && Ude_IsGuardAction(0x36) && !Ude_IsGuardAction(0x47);
    emit("unit", "unit", "policy_table_ranges", 0, "-", "-", "-", 0, ok, 0, "-", "-", "-");
}

/* ------------------------------------------------------------------ property campaign with shrinking */
static unsigned rng;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static const int kOpp[] = { A_NEUTRAL, A_MOVE, A_ATTACK, A_ATTACK + 9, A_TECH, A_RUSH, A_SKILL, A_CLASH, 0x38, 0xB9, 0x3FF };
static const unsigned kPhys[] = { 0, 0, 0, 0, 0x1, 0x2, 0x4, 0x8, 0x10, 0x40, 0x200, 0x4000, 0x80 | 0x8, 0x100000 };

typedef struct PFrame { UdeSnapshot s; unsigned phys; int enable; } PFrame;
static PFrame P[600];

/* returns the index of the first frame that breaks an invariant, -1 if none */
static int check(const PFrame *p, int n, const char **why) {
    UdeConfig c = def(); UdeState s; UdeDecision d; int i, run = 0;
    Ude_Reset(&s);
    for (i = 0; i < n; i++) {
        unsigned out;
        c.enabled = p[i].enable;
        out = Ude_Step(&s, &c, &p[i].s, p[i].phys, &d);
        if ((out & p[i].phys) != p[i].phys) { *why = "physical bit cleared"; return i; }
        if (out & ~p[i].phys & ~UDE_B_GUARD) { *why = "non-guard bit added"; return i; }
        if (d.injected && (p[i].phys & ~(UDE_B_DIR_MASK | UDE_B_DERIVED))) { *why = "injected over manual"; return i; }
        if (d.injected && (p[i].s.online || p[i].s.replay || !p[i].s.valid || !p[i].enable || p[i].s.cpu_side)) { *why = "injected while unsafe"; return i; }
        if (d.injected && (p[i].phys & UDE_B_DIR_MASK)) { *why = "injected while moving"; return i; }
        run = d.injected ? run + 1 : 0;
        if (run > c.hold_cap + c.hold_tail) { *why = "guard held past cap"; return i; }
    }
    return -1;
}

static void campaign(int runs, unsigned seed0) {
    int r, fails = 0;
    long frames = 0;
    for (r = 0; r < runs; r++) {
        unsigned seed = seed0 + (unsigned)r; int n, i, at; const char *why = "";
        rng = seed; n = 50 + (int)(rnd() % 500);
        for (i = 0; i < n; i++) {
            UdeSnapshot *s = &P[i].s; memset(s, 0, sizeof *s);
            s->valid = rnd() % 200 != 0; s->online = rnd() % 300 == 0; s->replay = rnd() % 300 == 0; s->cpu_side = rnd() % 400 == 0;
            s->fighting = rnd() % 50 != 0; s->time_stopped = rnd() % 80 == 0;
            s->my_action = (rnd() % 3) ? A_NEUTRAL : kOpp[rnd() % 11]; s->opp_action = kOpp[rnd() % 11];
            s->close = rnd() % 2; s->incoming_blast = rnd() % 6 == 0; s->behind = (int)(rnd() % 3) - 1;
            s->opp_attacking = rnd() % 3 == 0; s->opp_skill_kind = (int)(rnd() % 4) - 1; s->my_ki = (int)(rnd() % 100000);
            P[i].phys = (rnd() % 3) ? 0 : kPhys[rnd() % 14];
            P[i].enable = rnd() % 100 != 0;
        }
        frames += n;
        at = check(P, n, &why);
        if (at >= 0) {
            /* shrink: drop frames while the failure persists (delta debugging, one frame at a time) */
            int m = at + 1, k; char err[160];
            for (k = 0; k < m; ) {
                PFrame save = P[k]; const char *w2 = "";
                memmove(&P[k], &P[k + 1], sizeof(PFrame) * (m - k - 1));
                if (check(P, m - 1, &w2) >= 0) { m--; } else { memmove(&P[k + 1], &P[k], sizeof(PFrame) * (m - k - 1)); P[k] = save; k++; }
            }
            snprintf(err, sizeof err, "%s; minimal case %d frames", why, m);
            emit("property", "logic_sim", "invariants", seed, "random", "random", "-", at, 0, 0, "-", err, "regression needed");
            fails++;
        }
    }
    { char err[96]; snprintf(err, sizeof err, "%d runs %ld frames %d failures", runs, frames, fails);
      emit("property", "logic_sim", "invariants_campaign", seed0, "random", "random", "-", 0, fails == 0, 0, "-", err, "-"); }
}

/* ------------------------------------------------------------------ latency */
static void bench(void) {
    UdeConfig c = def(); UdeState s; UdeSnapshot n; UdeDecision d; long i, N2 = 20000000; struct timespec a, b; double ns;
    volatile unsigned sink = 0;
    memset(&n, 0, sizeof n); n.valid = 1; n.fighting = 1; n.my_action = A_NEUTRAL; n.close = 1; n.my_ki = 1; n.opp_skill_kind = -1;
    Ude_Reset(&s);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (i = 0; i < N2; i++) { n.opp_action = (int)(i & 0xFF); sink += Ude_Step(&s, &c, &n, (unsigned)(i & 3) == 0 ? 0 : 0, &d); }
    clock_gettime(CLOCK_MONOTONIC, &b);
    ns = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (double)N2;
    { char err[160]; snprintf(err, sizeof err, "%.1f ns per Ude_Step (host -O2); frame budget 33333333 ns at 30 Hz; state %zu bytes; config %zu bytes",
                             ns, sizeof(UdeState), sizeof(UdeConfig));
      emit("perf", "benchmark", "ude_step_latency", 0, "-", "-", "-", 0, ns < 1000.0, 0, "-", err, "-"); }
    (void)sink;
}

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 2000;
    unsigned seed = argc > 2 ? (unsigned)strtoul(argv[2], 0, 10) : 20261101u;
    printf("suite,level,name,seed,threat,initial,response,resp_frame,result,damage,affected,errors,cause\n");
    units();
    scenarios();
    campaign(runs, seed);
    bench();
    return gFail;
}
