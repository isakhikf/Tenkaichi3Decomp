/*
 * UDE Hybrid v5 tests: the Codex R8 recipes in the off-line engine. LEVEL: unit + logic simulation on the host.
 * Nothing here is evidence of a defence in the game: "accepted" below means accepted by the INPUT MODEL of this file
 * (a copy of the conditions of src/battle/btl_input.c and of the windows of BtlMove_UpdateDefenseTimers), not by the
 * game. Every recipe is tested alone, then in consecutive combinations, then against the arbitration rules.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "ude.h"

static int fails, total;
static unsigned xs(unsigned *r) { *r ^= *r << 13; *r ^= *r >> 17; *r ^= *r << 5; return *r; }
static void emit(const char *n, int ok, const char *d) {
    printf("v5,logic_sim,%s,0,-,-,-,0,%s,0,-,%s,-\n", n, ok ? "PASS" : "FAIL", d);
    fails += !ok;
    total++;
}

/* ---- the game's input conditions on the battle word (btl_input.c BtlInput_BuildCommands / condition switch) */
#define B_GUARD 0x1u
#define B_DASH 0x2u
#define B_BLAST 0x4u
#define B_RUSH 0x8u
#define B_UP 0x10u
#define B_DOWN 0x20u
#define B_LEFT 0x40u
#define B_RIGHT 0x80u
#define B_ANY 0x100000u
#define B_BLAST2 0x1000000u
static int cond38(unsigned h, unsigned p) { unsigned pr = h & ~p; return (pr & B_UP) && (pr & B_RUSH); }          /* Z */
static int cond39(unsigned h, unsigned p) { return ((h & ~p) & B_GUARD) != 0; }                                   /* fresh guard */
static int cond95(unsigned h, unsigned p) { return (h & B_GUARD) && ((h & ~p) & B_BLAST2); }                       /* Sway */
static int cond43(unsigned h, unsigned p) { return (h & B_GUARD) && ((h & ~p) & (B_LEFT | B_RIGHT)); }             /* GUARD_SIDE */
static int dash_side(unsigned h, unsigned p) { return ((h & ~p) & B_DASH) && (h & (B_LEFT | B_RIGHT)); }

/* ---- BtlMove_UpdateDefenseTimers (src/battle/btl_char_move.c:1620), runs after the input sample of the same update.
   w[0..5] = +0x1068, +0x106C, +0x1070, +0x1074, +0x1078, +0x107C; `h` this update's word, `p` the previous one. */
static void native_timers(int *w, unsigned h, unsigned p, int flag5, int caps) {
    int on = cond39(h, p) && flag5, z = cond38(h, p), side = cond43(h, p) && (caps & 0x20);
    if (--w[0] < -30) { w[0] = -30; if (on) w[0] = 4; } else if (on) w[0] = -1;
    if (--w[1] < -30) { w[1] = -30; if (on) w[1] = 2; } else if (on) w[1] = -1;
    if (--w[2] < -30) { w[2] = -30; if (z) w[2] = 1; } else if (z) w[2] = -1;
    if (--w[3] < -15) { w[3] = -15; if (side) w[3] = 5; }
    if (--w[4] < -30) { w[4] = -30; if (on) w[4] = 5; }
    if (--w[5] < -30) { w[5] = -30; if (on) w[5] = 2; } else if (on) w[5] = -1;
}

/* ---- snapshot helpers */
static void base(UdeSnapshot *s) {
    int k;
    memset(s, 0, sizeof *s);
    s->valid = s->fighting = s->have_ext = s->have_recipe = 1;
    s->my_action = 0x0B; s->opp_action = 0x0B; s->opp_skill_kind = -1; s->behind = 0; s->my_ki = 50000; s->my_hp = 30000;
    s->flag5 = 1; s->opp_attack_id = -1; s->blast_eta10 = -1; s->my_request_pending = 0; s->game_free = 1;
    for (k = 0; k < UDE_W_COUNT; k++) s->win[k] = -30;
    for (k = 0; k < UDE_X_COUNT; k++) s->recipe[k] = 0;
    s->recipe[UDE_X_OK] = 1; s->recipe[UDE_X_FREE] = 1; s->recipe[UDE_X_FLAG5] = 1; s->recipe[UDE_X_MY_ACT] = 0x0B;
    s->recipe[UDE_X_OPP_ACT] = 0x0B; s->recipe[UDE_X_OWN_FLAGS] = 0xB1; s->recipe[UDE_X_MY_KI] = 50000;
    s->recipe[UDE_X_W1068] = s->recipe[UDE_X_W106C] = s->recipe[UDE_X_W1070] = s->recipe[UDE_X_W1078] = s->recipe[UDE_X_W107C] = -30;
    s->recipe[UDE_X_W1074] = -15;
    s->recipe[UDE_X_UNTIL100] = s->recipe[UDE_X_UNTIL_FLOOR100] = UDE_X_NONE100; s->recipe[UDE_X_INC_ETA100] = UDE_X_NONE100;
    s->recipe[UDE_X_CAST_ETA100] = -1; s->recipe[UDE_X_STUN_REACT] = s->recipe[UDE_X_STUN_DIR] = -1; s->recipe[UDE_X_ATK] = -1;
    s->recipe[UDE_X_SLOT] = s->recipe[UDE_X_RUSHTYPE] = -1; s->recipe[UDE_X_FIRST_EVENT] = -1; s->recipe[UDE_X_OPP_ANIM] = 0x80;
}
/* a frontal strike whose first impact event is `n` updates away */
static void melee(UdeSnapshot *s, int n, int atk) {
    s->opp_action = 0x70; s->close = 1; s->opp_attack_id = atk; s->opp_attack_flags = 1;
    s->recipe[UDE_X_OPP_ACT] = 0x70; s->recipe[UDE_X_ATK] = atk; s->recipe[UDE_X_ATK_FLAGS] = 1; s->recipe[UDE_X_MELEE] = 1;
    s->recipe[UDE_X_FIRST_EVENT] = 20; s->recipe[UDE_X_FIRST_HIT] = 1; s->recipe[UDE_X_DODGE_WIN] = 1;
    s->recipe[UDE_X_UNTIL100] = s->recipe[UDE_X_UNTIL_FLOOR100] = n * 100; s->recipe[UDE_X_OPP_FRAME100] = (20 - n) * 100;
    s->recipe[UDE_X_D_LT_R] = s->recipe[UDE_X_D_LT_R2] = s->recipe[UDE_X_D_LT_R4] = s->recipe[UDE_X_D_LT_100] = s->recipe[UDE_X_D_LT_140] = s->recipe[UDE_X_D_LT_180] = 1;
    s->recipe[UDE_X_URGENT] = 1;
}
/* an ordinary ki blast arriving in eta100 */
static void blast(UdeSnapshot *s, int eta100, int bits) {
    s->incoming_blast = 1; s->blast_eta10 = eta100 / 10;
    s->recipe[UDE_X_INC] = s->recipe[UDE_X_VOLLEY] = s->recipe[UDE_X_PENDING24] = UDE_INC_HIT | bits;
    s->recipe[UDE_X_INC_ETA100] = eta100; s->recipe[UDE_X_PROJ_FIRST] = 1; s->recipe[UDE_X_OPP_ACT] = 0xAE; s->opp_action = 0xAE;
}

typedef struct { UdeConfig c; UdeState st; unsigned prev; int fired_k[UDE_K_COUNT]; int cond_hits[5]; } Run;
static void run_init(Run *r) {
    Ude_DefaultConfig(&r->c);
    r->c.recipes = 1;
    r->c.manual_claim = 1;
    Ude_Reset(&r->st);
    r->prev = 0;
    memset(r->fired_k, 0, sizeof r->fired_k);
    memset(r->cond_hits, 0, sizeof r->cond_hits);
}
static unsigned step(Run *r, const UdeSnapshot *s, unsigned phys, UdeDecision *d) {
    unsigned out = Ude_Step(&r->st, &r->c, s, phys, d);
    if (cond38(out, r->prev)) r->cond_hits[0]++;
    if (cond39(out, r->prev)) r->cond_hits[1]++;
    if (cond95(out, r->prev)) r->cond_hits[2]++;
    if (cond43(out, r->prev)) r->cond_hits[3]++;
    if (dash_side(out, r->prev)) r->cond_hits[4]++;
    if (d->press_edge && d->recipe_kind > 0) r->fired_k[d->recipe_kind]++;
    r->prev = out;
    return out;
}

int main(void) {
    Run r;
    UdeSnapshot s;
    UdeDecision d;
    unsigned out;
    char det[512];
    int n, ok, k;

    /* ---------------- translation table */
    ok = 1;
    for (k = 0; k < 0x10000; k++) {
        unsigned raw = (unsigned)k & (UDE_P_UP | UDE_P_RIGHT | UDE_P_DOWN | UDE_P_LEFT | UDE_P_RUSH | UDE_P_DASH | UDE_P_GUARD | UDE_P_BLAST | UDE_P_CHARGE);
        if (UdeRecipe_FromBattle(UdeRecipe_ToBattle(raw)) != raw) ok = 0;
    }
    emit("v5_bits_roundtrip", ok, "raw pad -> battle word -> raw is the identity for every recipe bit");
    emit("v5_bits_directions", UdeRecipe_ToBattle(UDE_P_RIGHT) == B_RIGHT && UdeRecipe_ToBattle(UDE_P_LEFT) == B_LEFT &&
         UdeRecipe_ToBattle(UDE_P_DOWN) == B_DOWN && UdeRecipe_ToBattle(UDE_P_UP) == B_UP,
         "PAD_RIGHT 0x20 -> BTLB_RIGHT 0x80, PAD_LEFT 0x80 -> BTLB_LEFT 0x40, PAD_DOWN 0x40 -> BTLB_DOWN 0x20 (include/sys/pad.h)");
    emit("v5_bits_derived", (UdeRecipe_ToBattle(UDE_P_BLAST) & B_BLAST2) && (UdeRecipe_ToBattle(UDE_P_GUARD) & B_ANY),
         "BLAST also sets BLAST2 (condition 95), any face button sets ANY_FACE, as BtlInput_BuildRecord does for injected input");

    /* ---------------- 1 Z-Counter: pressed on the update of the first impact, never before */
    run_init(&r);
    for (n = 6, ok = 1; n >= 1; n--) {
        base(&s); melee(&s, n, 3); s.recipe[UDE_X_CAPS] = 0;     /* no Sway capability: Z is the answer */
        out = step(&r, &s, 0, &d);
        if (n > 1 && cond38(out, B_GUARD & 0)) ok = ok && !(out & B_RUSH);
    }
    snprintf(det, sizeof det, "condition 38 hits %d, Z presses %d, last word 0x%X", r.cond_hits[0], r.fired_k[UDE_K_ZCOUNTER], out);
    emit("v5_z_on_impact_update", ok && r.cond_hits[0] == 1 && (out & (B_RUSH | B_UP)) == (B_RUSH | B_UP) && r.fired_k[UDE_K_ZCOUNTER] == 1, det);
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_W1070] = -12; step(&r, &s, 0, &d); }
    emit("v5_z_needs_window_floor", r.cond_hits[0] == 0, "counter window above its floor (-12): a press would close it; no Z");
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_FIRST_HIT] = 0; step(&r, &s, 0, &d); }
    emit("v5_z_first_hit_only", r.cond_hits[0] == 0, "a later hit volume of the combo: no Z (BtlHit_CheckGuard accepts the first hit only)");
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_OWN_FLAGS] = 0; step(&r, &s, 0, &d); }
    emit("v5_z_needs_0x31_motion", r.cond_hits[0] == 0, "own animation without flag 0x31: no Z");
    run_init(&r); r.c.rcfg.kinds &= ~(1u << (UDE_K_ZCOUNTER - 1));
    for (n = 6; n >= 1; n--) { base(&s); melee(&s, n, 3); step(&r, &s, 0, &d); }
    emit("v5_z_switch_off", r.cond_hits[0] == 0 && r.fired_k[UDE_K_ZCOUNTER] == 0, "ude_r_z=0: no Z");

    /* ---------------- 3 Sonic Sway: frontal strike < 11, flags2 0x80, ki > 1000, idle */
    run_init(&r);
    for (n = 8; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_CAPS] = 0x80; step(&r, &s, 0, &d); }
    snprintf(det, sizeof det, "condition 95 hits %d, Sway presses %d", r.cond_hits[2], r.fired_k[UDE_K_SWAY]);
    emit("v5_sway_preferred_over_z", r.cond_hits[2] == 1 && r.cond_hits[0] == 0 && r.fired_k[UDE_K_SWAY] == 1, det);
    run_init(&r);
    for (n = 8; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_CAPS] = 0x80; s.recipe[UDE_X_MY_KI] = 900; step(&r, &s, 0, &d); }
    emit("v5_sway_needs_ki", r.cond_hits[2] == 0 && r.cond_hits[0] == 1, "ki 900 (<= 1000): no Sway, the Z-Counter instead");
    run_init(&r);
    for (n = 8; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_CAPS] = 0x80; s.recipe[UDE_X_BACK] = 1; s.behind = 1; step(&r, &s, 0, &d); }
    emit("v5_sway_front_only", r.cond_hits[2] == 0, "attacker behind: no Sway");
    run_init(&r);
    for (n = 8; n >= 1; n--) { base(&s); melee(&s, n, 12); s.recipe[UDE_X_CAPS] = 0x80; step(&r, &s, 0, &d); }
    emit("v5_sway_attack_below_11", r.cond_hits[2] == 0, "attack id 12: outside the Sway's attacks");
    run_init(&r);
    for (n = 8; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_CAPS] = 0x80; s.recipe[UDE_X_OPP_UNK2] = 4; step(&r, &s, 0, &d); }
    emit("v5_sway_attacker_param", r.cond_hits[2] == 0, "attacker parameter 2 == 4: the game refuses the Sway (btl_char_hit.c:628)");
    /* the stance 0x67 is kept with guard while the strike lasts */
    run_init(&r); base(&s); melee(&s, 2, 3); s.recipe[UDE_X_MY_ACT] = s.my_action = 0x67; out = step(&r, &s, 0, &d);
    emit("v5_sway_stance_held", (out & B_GUARD) && !(out & B_BLAST), "in stance 0x67 against the strike: guard only");

    /* ---------------- 2 evasion */
    run_init(&r); r.c.rcfg.kinds &= ~(1u << (UDE_K_ZCOUNTER - 1));
    for (n = 6; n >= 1; n--) { base(&s); melee(&s, n, 3); step(&r, &s, 0, &d); }
    snprintf(det, sizeof det, "condition 39 hits %d, evade presses %d", r.cond_hits[1], r.fired_k[UDE_K_EVADE]);
    emit("v5_evade_on_impact_update", r.fired_k[UDE_K_EVADE] == 1 && r.cond_hits[1] >= 1, det);
    run_init(&r); r.c.rcfg.kinds &= ~(1u << (UDE_K_ZCOUNTER - 1));
    for (n = 6; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_W1068] = -5; step(&r, &s, 0, &d); }
    emit("v5_evade_window_cooling", r.fired_k[UDE_K_EVADE] == 0, "evade window cooling (-5): no press");
    run_init(&r); r.c.rcfg.kinds &= ~(1u << (UDE_K_ZCOUNTER - 1));
    for (n = 6; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_ATK_FLAGS] = 0; s.opp_attack_flags = 0; step(&r, &s, 0, &d); }
    emit("v5_evade_needs_flag1", r.fired_k[UDE_K_EVADE] == 0, "attack without flag 1 (not evadable): no evade press");

    /* ---------------- 9 throw break */
    run_init(&r);
    for (n = 5; n >= 0; n--) {
        base(&s); s.opp_action = 0xB4; s.opp_attack_id = 0x55; s.close = 1;
        s.recipe[UDE_X_OPP_ACT] = 0xB4; s.recipe[UDE_X_ATK] = 0x55; s.recipe[UDE_X_THROWING] = 1; s.recipe[UDE_X_FIRST_EVENT] = 10;
        s.recipe[UDE_X_UNTIL100] = s.recipe[UDE_X_UNTIL_FLOOR100] = n * 100; s.recipe[UDE_X_OPP_FRAME100] = (10 - n) * 100;
        s.recipe[UDE_X_D_LT_R2] = s.recipe[UDE_X_D_LT_R4] = s.recipe[UDE_X_D_LT_R] = 1; s.recipe[UDE_X_OWN_FLAGS] = 0x31;
        out = step(&r, &s, 0, &d);
    }
    emit("v5_throw_break", r.fired_k[UDE_K_THROWBREAK] == 1 && r.cond_hits[1] >= 1, "fresh guard 2 updates before the throw (window +0x1078: 5 updates)");
    run_init(&r);
    for (n = 5; n >= 0; n--) {
        base(&s); s.opp_action = 0xB4; s.recipe[UDE_X_OPP_ACT] = 0xB4; s.recipe[UDE_X_ATK] = 0x55; s.recipe[UDE_X_THROWING] = 1;
        s.recipe[UDE_X_FIRST_EVENT] = 10; s.recipe[UDE_X_UNTIL100] = s.recipe[UDE_X_UNTIL_FLOOR100] = n * 100; s.recipe[UDE_X_D_LT_R2] = 1;
        s.recipe[UDE_X_OWN_FLAGS] = 0x31; s.recipe[UDE_X_BACK] = 1; s.behind = 1; step(&r, &s, 0, &d);
    }
    emit("v5_throw_from_behind_no_break", r.fired_k[UDE_K_THROWBREAK] == 0, "from behind the break is impossible: no throw-break press");

    /* ---------------- 10 rush evasion */
    run_init(&r);
    for (n = 6; n >= 0; n--) {
        base(&s); s.opp_action = 0x12D; s.recipe[UDE_X_OPP_ACT] = 0x12D; s.recipe[UDE_X_RUSHING] = 1;
        s.recipe[UDE_X_UNTIL100] = s.recipe[UDE_X_UNTIL_FLOOR100] = n * 100; step(&r, &s, 0, &d);
    }
    emit("v5_rush_evade", r.fired_k[UDE_K_RUSHEVADE] == 1, "fresh guard 1 update before a rush super arrives (window +0x107C)");

    /* ---------------- 4 deflect, stance renewal, 5 charged, reflect/absorb by type */
    run_init(&r);
    for (n = 12; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_REDIRECT | UDE_INC_ABSORB); s.recipe[UDE_X_CAPS] = 0x20; step(&r, &s, 0, &d); }
    snprintf(det, sizeof det, "condition 43 hits %d, deflect presses %d", r.cond_hits[3], r.fired_k[UDE_K_DEFLECT]);
    emit("v5_deflect_front_ki", r.cond_hits[3] >= 1 && r.fired_k[UDE_K_DEFLECT] >= 1, det);
    run_init(&r);
    for (k = 0, ok = 0; k < 10; k++) {
        base(&s); blast(&s, 300, UDE_INC_REDIRECT | UDE_INC_ABSORB); s.recipe[UDE_X_CAPS] = 0x20; s.recipe[UDE_X_MY_ACT] = s.my_action = 0x3B;
        out = step(&r, &s, 0, &d); ok += cond43(out, 0) ? 0 : 0;
    }
    snprintf(det, sizeof det, "GUARD_SIDE edges in 10 updates of stance 0x3B: %d", r.cond_hits[3]);
    emit("v5_deflect_stance_renewal", r.cond_hits[3] >= 4 && (out & B_GUARD), det);
    run_init(&r); base(&s); blast(&s, 300, UDE_INC_REDIRECT); s.recipe[UDE_X_CAPS] = 0x20; s.recipe[UDE_X_MY_ACT] = s.my_action = 0x3B;
    s.recipe[UDE_X_VOLLEY] = 0; s.recipe[UDE_X_INC] = 0; s.recipe[UDE_X_BACK] = 1; s.behind = 1; out = step(&r, &s, 0, &d);
    emit("v5_deflect_stance_behind_released", out == 0, "stance 0x3B with the attacker behind: release (the stance only deflects frontal ki)");
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_REDIRECT | UDE_INC_CHARGED); s.recipe[UDE_X_CAPS] = 0x20; s.recipe[UDE_X_W1074] = 0; step(&r, &s, 0, &d); }
    emit("v5_charged_guard", r.fired_k[UDE_K_CHARGED] == 1, "charged ki blast: fresh guard 2 updates before (recipe 5)");
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_REDIRECT); s.recipe[UDE_X_CAPS] = 0x20; s.recipe[UDE_X_PFLAGS] = 2; step(&r, &s, 0, &d); }
    emit("v5_absorber_side_dash", r.fired_k[UDE_K_SIDESTEP] == 1 && r.cond_hits[4] == 1, "character with parameter flag 2 against a non-absorbable blast: dash aside (recipe 6)");
    /* the same absorbing character against an absorbable blast: GUARD + side (the game queues the absorb stance 0x3D
       for parameter flag 2, btl_act_h_b.c:2286), never a dash */
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_REDIRECT | UDE_INC_ABSORB); s.recipe[UDE_X_CAPS] = 0x20; s.recipe[UDE_X_PFLAGS] = 2; step(&r, &s, 0, &d); }
    emit("v5_absorber_absorb", r.fired_k[UDE_K_DEFLECT] >= 1 && r.fired_k[UDE_K_SIDESTEP] == 0 && r.cond_hits[3] >= 1, "absorbing character, absorbable blast: GUARD + side (stance 0x3D), never a dash");
    /* a character without the deflect capability (parameter flags2 0x20 off): no deflect press (impossible) */
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_REDIRECT | UDE_INC_ABSORB); s.recipe[UDE_X_CAPS] = 0; step(&r, &s, 0, &d); }
    emit("v5_no_deflect_capability", r.fired_k[UDE_K_DEFLECT] == 0, "no flags2 0x20: no deflect press; the blast is guarded or dodged");
    /* a super technique's shot (not redirectable): never deflect */
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_TECH | UDE_INC_EVADABLE); s.recipe[UDE_X_CAPS] = 0x20; step(&r, &s, 0, &d); }
    emit("v5_technique_not_deflected", r.fired_k[UDE_K_DEFLECT] == 0 && r.fired_k[UDE_K_SIDESTEP] == 1, "super technique shot (not redirectable): no deflect; dash aside 4 updates out (Codex recipe 6)");
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_BEHIND); step(&r, &s, 0, &d); }
    emit("v5_blast_behind_side_dash", r.fired_k[UDE_K_SIDESTEP] == 1 && r.fired_k[UDE_K_DEFLECT] == 0, "blast from behind: never deflect (impossible), dash aside");

    /* ---------------- 14 VanishStep */
    run_init(&r);
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_BEHIND); s.recipe[UDE_X_VANISH] = 1; step(&r, &s, 0, &d); }
    if (getenv("DBG")) fprintf(stderr, "vanish fired %d side %d c43 %d c39 %d\n", r.fired_k[UDE_K_VANISH], r.fired_k[UDE_K_SIDESTEP], r.cond_hits[3], r.cond_hits[1]);
    emit("v5_vanish_step", r.fired_k[UDE_K_VANISH] == 1 && r.cond_hits[3] == 1, "blast from behind with the VanishStep available: GUARD + side, fresh");
    run_init(&r); r.c.rcfg.kinds &= ~(1u << (UDE_K_VANISH - 1));
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_BEHIND); s.recipe[UDE_X_VANISH] = 1; step(&r, &s, 0, &d); }
    emit("v5_vanish_switch_off", r.fired_k[UDE_K_VANISH] == 0, "ude_r_vanish=0: no VanishStep");

    /* ---------------- combinations: a three-hit string, Z on the first, guard on the rest; then a blast */
    run_init(&r);
    {
        int z = 0, guard_frames = 0, bad = 0, f;
        for (f = 0; f < 40; f++) {
            int hit = f / 10, n2 = 10 - (f % 10);
            base(&s); melee(&s, n2, 3); s.recipe[UDE_X_OPP_ANIM] = 0x80 + hit; s.recipe[UDE_X_FIRST_HIT] = hit == 0;
            if (hit == 3) { base(&s); blast(&s, n2 * 100, UDE_INC_REDIRECT | UDE_INC_ABSORB); s.recipe[UDE_X_CAPS] = 0x20; }
            out = step(&r, &s, 0, &d);
            z += cond38(out, 0) && d.recipe_kind == UDE_K_ZCOUNTER && d.press_edge;
            guard_frames += (out & B_GUARD) != 0;
            if (out & ~(B_GUARD | B_ANY | B_RUSH | B_UP | B_LEFT | B_RIGHT | B_BLAST | B_BLAST2 | B_DASH)) bad++;
        }
        snprintf(det, sizeof det, "Z %d, guard updates %d, deflect presses %d, illegal bits %d", z, guard_frames, r.fired_k[UDE_K_DEFLECT], bad);
        emit("v5_combo_string_then_blast", z == 1 && guard_frames > 10 && r.fired_k[UDE_K_DEFLECT] >= 1 && bad == 0, det);
    }
    /* two first hits in a row (two separate attacks): the second Z is armed again only after the native cooldown */
    run_init(&r);
    {
        int f, zs = 0;
        for (f = 0; f < 24; f++) {
            int a = f / 12, n2 = 12 - (f % 12);
            base(&s); melee(&s, n2 > 6 ? 6 : n2, 3); s.recipe[UDE_X_OPP_ANIM] = 0x90 + a; s.recipe[UDE_X_FIRST_EVENT] = 20 + a;
            if (a == 1 && f % 12 < 2) s.recipe[UDE_X_W1070] = -20;   /* still cooling from the first Z */
            out = step(&r, &s, 0, &d);
            zs += d.press_edge && d.recipe_kind == UDE_K_ZCOUNTER;
        }
        snprintf(det, sizeof det, "Z presses over two attacks: %d", zs);
        emit("v5_combo_two_attacks_two_z", zs == 2, det);
    }

    /* off-line pulse timing: a Z planned while RUSH + UP is still held from the previous packet releases for one
       update (n == 2) and presses on the impact update (n == 1), never after it */
    run_init(&r);
    {
        int z_at = -1, f;
        for (f = 0; f < 3; f++) {
            base(&s);
            if (f == 0) melee(&s, 1, 3);
            else { melee(&s, 3 - f, 3); s.recipe[UDE_X_OPP_ANIM] = 0x99; s.recipe[UDE_X_FIRST_EVENT] = 40; }
            out = step(&r, &s, 0, &d);
            if (f > 0 && cond38(out, r.st.rs.out_valid ? 0 : 0) && d.press_edge && d.recipe_kind == UDE_K_ZCOUNTER) z_at = 3 - f;
        }
        snprintf(det, sizeof det, "second Z pressed at n = %d (impact update = 1)", z_at);
        emit("v5_z_after_held_edge_on_time", z_at == 1, det);
    }
    /* Sway on a first attack (ki 50000), then the ki falls under the Sway's 1000: the Z-Counter on the next attack */
    run_init(&r);
    {
        int f, sway = 0, z = 0;
        for (f = 0; f < 24; f++) {
            int a = f / 12, n2 = 12 - (f % 12);
            base(&s); melee(&s, n2 > 6 ? 6 : n2, 3); s.recipe[UDE_X_CAPS] = 0x80; s.recipe[UDE_X_OPP_ANIM] = 0xA0 + a; s.recipe[UDE_X_FIRST_EVENT] = 30 + a;
            if (a == 1) s.recipe[UDE_X_MY_KI] = s.my_ki = 900;
            out = step(&r, &s, 0, &d);
            sway += d.press_edge && d.recipe_kind == UDE_K_SWAY;
            z += d.press_edge && d.recipe_kind == UDE_K_ZCOUNTER;
        }
        snprintf(det, sizeof det, "Sway %d then Z %d", sway, z);
        emit("v5_combo_sway_then_z", sway == 1 && z == 1, det);
    }
    /* Z on a strike, then a blast from behind with the VanishStep available */
    run_init(&r);
    {
        int f, z = 0, v = 0;
        for (f = 0; f < 20; f++) {
            base(&s);
            if (f < 8) melee(&s, 8 - f > 6 ? 6 : 8 - f, 3);
            else { blast(&s, (20 - f) * 100, UDE_INC_BEHIND); s.recipe[UDE_X_VANISH] = 1; s.recipe[UDE_X_BACK] = s.behind = 1; }
            out = step(&r, &s, 0, &d);
            z += d.press_edge && d.recipe_kind == UDE_K_ZCOUNTER;
            v += d.press_edge && d.recipe_kind == UDE_K_VANISH;
        }
        snprintf(det, sizeof det, "Z %d, then VanishStep %d", z, v);
        emit("v5_combo_z_then_vanish", z == 1 && v == 1, det);
    }
    /* a second blast from behind whose whole window falls within 5 updates of the first VanishStep: one VanishStep
       (Codex: no new pulse within 10 blanks = 5 updates) */
    run_init(&r);
    {
        int f, v = 0;
        for (f = 0; f < 6; f++) {   /* the second blast hits on update 6: it is gone after it */
            base(&s); blast(&s, (f < 2 ? 4 - f : 6 - f) * 100, UDE_INC_BEHIND); s.recipe[UDE_X_VANISH] = 1;
            out = step(&r, &s, 0, &d);
            v += d.press_edge && d.recipe_kind == UDE_K_VANISH;
        }
        snprintf(det, sizeof det, "VanishStep presses %d", v);
        emit("v5_vanish_spacing", v == 1, det);
    }
    /* the player presses on the update of the planned release: the plan goes; no stale Z after the grace */
    run_init(&r);
    {
        int f, late = 0, z = 0;
        for (f = 0; f < 14; f++) {
            base(&s); melee(&s, 7 - f, 3);
            if (f >= 7) { base(&s); }
            out = step(&r, &s, f == 4 ? B_GUARD : 0, &d);
            z += d.press_edge && d.recipe_kind == UDE_K_ZCOUNTER;
            if (f > 4 && d.injected && d.recipe_kind == UDE_K_ZCOUNTER) late++;
        }
        snprintf(det, sizeof det, "Z after the player's own guard press: %d", late);
        emit("v5_manual_drops_plan", late == 0, det);
    }
    /* Etapa C: the native evasion of a strike has no facing test (BtlHit_CheckDodge): it is also planned from behind */
    run_init(&r);
    for (n = 5; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_BACK] = s.behind = 1; s.recipe[UDE_X_W1070] = -12; step(&r, &s, 0, &d); }
    emit("v5_evade_from_behind", r.fired_k[UDE_K_EVADE] == 1 && r.fired_k[UDE_K_ZCOUNTER] == 0, "strike from behind, Z window cooling: evade press only");
    /* a strike that will not reach (no impact in reach): nothing */
    run_init(&r);
    for (n = 5; n >= 1; n--) { base(&s); melee(&s, n, 3); s.recipe[UDE_X_MELEE] = 0; s.recipe[UDE_X_UNTIL100] = s.recipe[UDE_X_UNTIL_FLOOR100] = UDE_X_NONE100; s.close = 0; step(&r, &s, 0, &d); }
    emit("v5_out_of_reach_nothing", r.fired_k[UDE_K_EVADE] == 0 && r.fired_k[UDE_K_ZCOUNTER] == 0, "attack out of reach: no recipe press");
    /* no stuck buttons: after any threat, the word is empty again within 4 idle updates (v4 release tail 3) */
    {
        unsigned rng2 = 77u;
        int seq, stuck = 0;
        for (seq = 0; seq < 3000; seq++) {
            int f, len = 2 + (int)(xs(&rng2) % 12);
            run_init(&r);
            for (f = 0; f < len + 6; f++) {
                base(&s);
                if (f < len) switch (xs(&rng2) % 3) {
                    case 0: melee(&s, (int)(xs(&rng2) % 6), 3); break;
                    case 1: blast(&s, (int)(xs(&rng2) % 800), (int)(xs(&rng2) % 256) & ~1); s.recipe[UDE_X_CAPS] = 0x20; break;
                    default: s.recipe[UDE_X_THROWING] = 1; s.recipe[UDE_X_ATK] = 0x55; s.recipe[UDE_X_D_LT_R2] = 1;
                             s.recipe[UDE_X_UNTIL100] = s.recipe[UDE_X_UNTIL_FLOOR100] = (int)(xs(&rng2) % 4) * 100; break;
                }
                out = step(&r, &s, 0, &d);
                if (f >= len + 4 && out) stuck++;
            }
        }
        snprintf(det, sizeof det, "3000 threat sequences: word not empty 4 updates after the threat in %d", stuck);
        emit("v5_no_stuck_buttons", stuck == 0, det);
    }

    /* ude_min_ki: below it, no recipe either (the player's ki stays his) */
    run_init(&r); r.c.min_ki = 60000;
    for (n = 6; n >= 1; n--) { base(&s); blast(&s, n * 100, UDE_INC_BEHIND); s.recipe[UDE_X_VANISH] = 1; step(&r, &s, 0, &d); }
    emit("v5_min_ki_respected", r.fired_k[UDE_K_VANISH] == 0 && r.st.stats.recipe_frames == 0, "ki 50000 under ude_min_ki 60000: no VanishStep, no recipe");

    /* ---------------- arbitration */
    run_init(&r);
    {
        int f, lost = 0, inj_while = 0;
        for (f = 0; f < 30; f++) {
            unsigned ph = (f >= 10 && f < 14) ? B_RUSH : 0;
            base(&s); melee(&s, 30 - f > 6 ? 6 : 30 - f, 3);
            out = step(&r, &s, ph, &d);
            if ((out & ph) != ph) lost++;
            if (ph && d.injected) inj_while++;
        }
        emit("v5_manual_always_wins", lost == 0 && inj_while == 0, "physical RUSH during a planned Z: never cleared, never mixed");
    }
    run_init(&r);
    {
        int f, inj = 0;
        for (f = 0; f < 12; f++) {
            base(&s); melee(&s, 1, 3);
            s.my_action = s.recipe[UDE_X_MY_ACT] = (f < 8) ? 0x75 : 0x0B;   /* the player's own attack lasts until update 8 */
            out = step(&r, &s, f == 0 ? B_RUSH : 0, &d);
            if (f > 0 && f < 8) inj += d.injected;
        }
        emit("v5_manual_claim", inj == 0 && r.st.stats.claims > 0, "the player's own attack (0x75) still running after the 4-update grace: the engine stays out");
    }
    run_init(&r); base(&s); melee(&s, 1, 3); out = step(&r, &s, B_LEFT, &d);
    emit("v5_moving_blocks", !d.injected && out == B_LEFT, "a direction held: nothing added (ude_moving=0)");
    run_init(&r); base(&s); melee(&s, 1, 3); s.my_request_pending = 1; out = step(&r, &s, 0, &d);
    emit("v5_request_pending_blocks", !d.injected, "the player's own request queued: nothing added");
    run_init(&r); base(&s); melee(&s, 1, 3); s.online = 1; out = step(&r, &s, 0, &d);
    emit("v5_never_online", !d.injected && out == 0, "online: suspended");
    run_init(&r); base(&s); melee(&s, 1, 3); s.replay = 1; out = step(&r, &s, 0, &d);
    emit("v5_never_replay", !d.injected, "replay: suspended");
    run_init(&r); base(&s); melee(&s, 1, 3); s.cpu_side = 1; out = step(&r, &s, 0, &d);
    emit("v5_never_cpu_side", !d.injected, "CPU-controlled side: suspended");
    run_init(&r); r.c.enabled = 0; base(&s); melee(&s, 1, 3); out = step(&r, &s, 0, &d);
    emit("v5_toggle_off", !d.injected, "F7 off: nothing");
    /* recipes off = v4 exactly */
    {
        UdeConfig a, b; UdeState sa, sb; UdeDecision da, db; int f, same = 1;
        Ude_DefaultConfig(&a); Ude_DefaultConfig(&b); b.recipes = 0; b.manual_claim = 0; Ude_Reset(&sa); Ude_Reset(&sb);
        for (f = 0; f < 200; f++) {
            unsigned oa, ob;
            base(&s); if (f % 20 < 10) melee(&s, 10 - f % 10, 3); else blast(&s, (20 - f % 20) * 100, UDE_INC_REDIRECT);
            oa = Ude_Step(&sa, &a, &s, (f % 37 == 0) ? B_DASH : 0, &da);
            ob = Ude_Step(&sb, &b, &s, (f % 37 == 0) ? B_DASH : 0, &db);
            if (oa != ob || da.reason != db.reason) same = 0;
        }
        emit("v5_off_is_v4", same, "ude_recipes=0: the same word and reason as the v4 engine on 200 updates");
    }

    /* ---------------- property campaign: random snapshots and pads */
    {
        unsigned rng = 2026u;
        long frames = 0, bad_clear = 0, bad_manual = 0, bad_bits = 0, bad_mash = 0, presses = 0;
        int seq, q;
        for (seq = 0; seq < 4000; seq++) {
            int f, w[6];
            run_init(&r);
            for (q = 0; q < 6; q++) w[q] = (xs(&rng) % 3) ? (q == 3 ? -15 : -30) : (int)(xs(&rng) % 10) - 5;
            for (f = 0; f < 60; f++) {
                unsigned ph = 0;
#define RND() xs(&rng)
                base(&s);
                switch (RND() % 4) {
                case 0: melee(&s, (int)(RND() % 8), (int)(RND() % 14)); break;
                case 1: blast(&s, (int)(RND() % 1200), (int)(RND() % 256) & ~1); break;
                case 2: s.recipe[UDE_X_THROWING] = 1; s.recipe[UDE_X_ATK] = 0x55; s.recipe[UDE_X_UNTIL100] = s.recipe[UDE_X_UNTIL_FLOOR100] = (int)(RND() % 6) * 100;
                        s.recipe[UDE_X_D_LT_R2] = 1; break;
                default: break;
                }
                for (q = 0; q < 6; q++) s.recipe[UDE_X_W1068 + q] = w[q];   /* the windows evolve as the game's */
                s.recipe[UDE_X_CAPS] = (int)(RND() % 256); s.recipe[UDE_X_VANISH] = (int)(RND() % 2); s.recipe[UDE_X_BACK] = s.behind = (int)(RND() % 4 == 0);
                s.recipe[UDE_X_MY_ACT] = s.my_action = (RND() % 5 == 0) ? 0x3B : 0x0B;
                if (RND() % 9 == 0) ph = 1u << (RND() % 10);
                {
                    unsigned before = r.prev;
                    out = step(&r, &s, ph, &d);
                    native_timers(w, out, before, s.recipe[UDE_X_FLAG5], s.recipe[UDE_X_CAPS]);
                }
                frames++;
                if ((out & ph) != ph) bad_clear++;
                if (d.injected && (ph & ~(UDE_B_DIR_MASK | UDE_B_DERIVED))) bad_manual++;
                if (out & ~ph & ~(B_GUARD | B_ANY | B_RUSH | B_UP | B_DOWN | B_LEFT | B_RIGHT | B_BLAST | B_BLAST2 | B_DASH)) bad_bits++;
                /* anti-mash: a fresh guard / Z edge by a recipe while its window is above the floor */
                if (d.press_edge && d.origin == UDE_O_RECIPE) {
                    presses++;
                    if (getenv("DBG") && ((d.recipe_kind == UDE_K_ZCOUNTER && s.recipe[UDE_X_W1070] > -30) || (d.recipe_kind == UDE_K_EVADE && s.recipe[UDE_X_MELEE] && s.recipe[UDE_X_W1068] > -30) || (d.recipe_kind == UDE_K_THROWBREAK && s.recipe[UDE_X_W1078] > -30)))
                        fprintf(stderr, "mash kind %d w %d/%d/%d melee %d thr %d until %d act %d rr %d prev %x out %x\n", d.recipe_kind, s.recipe[UDE_X_W1068], s.recipe[UDE_X_W1070], s.recipe[UDE_X_W1078], s.recipe[UDE_X_MELEE], s.recipe[UDE_X_THROWING], s.recipe[UDE_X_UNTIL100], s.recipe[UDE_X_MY_ACT], d.recipe_result, r.st.rs.prev_out, out);
                    if (d.recipe_kind == UDE_K_ZCOUNTER && s.recipe[UDE_X_W1070] > -30) bad_mash++;
                    if (d.recipe_kind == UDE_K_EVADE && s.recipe[UDE_X_MELEE] && s.recipe[UDE_X_W1068] > -30) bad_mash++;
                    if (d.recipe_kind == UDE_K_THROWBREAK && s.recipe[UDE_X_W1078] > -30) bad_mash++;
                }
            }
        }
        snprintf(det, sizeof det, "frames %ld, recipe presses %ld: cleared physical %ld, manual mixed %ld, illegal bits %ld, press into a cooling window %ld",
                 frames, presses, bad_clear, bad_manual, bad_bits, bad_mash);
        emit("v5_property_invariants", bad_clear == 0 && bad_manual == 0 && bad_bits == 0 && bad_mash == 0 && presses > 0, det);
    }
    /* performance (host only; the game PC's figure comes from the SUMMARY lines of ude_log=1) */
    {
        struct timespec a, b;
        long it, N = 2000000;
        double ns;
        run_init(&r);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (it = 0; it < N; it++) {
            base(&s);
            if (it & 1) blast(&s, (int)(it % 9) * 100, UDE_INC_REDIRECT); else melee(&s, (int)(it % 7), 3);
            step(&r, &s, 0, &d);
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        ns = ((double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec)) / (double)N;
        snprintf(det, sizeof det, "Ude_Step with recipes: %.0f ns per update on this host (snapshot fill included; budget 33 ms)", ns);
        emit("v5_perf_step", ns < 5000.0, det);
    }
    fprintf(stderr, "v5: %d tests, %d failed\n", total, fails);
    return fails;
}
