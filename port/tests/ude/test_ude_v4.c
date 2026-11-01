/*
 * UDE cycle-5 tests: knock-away recovery (ude_recover). LEVEL: unit + logic simulation on the host.
 * The model copies BtlAct_BlowAwayHandler's decide phase (src/battle/btl_act_a.c ~3257..3283): free, actionFrame > 8,
 * character able to recover; every NEW press of battle bit ANY_FACE (0x100000) counts; at (1 - health) x 12 + 4 presses
 * the game requests 0xD7. Plus the invariants every response must keep.
 */
#include <stdio.h>
#include <string.h>
#include "ude.h"
static int fails;
static void emit(const char *n, int ok, const char *d) { printf("v4,logic_sim,%s,0,-,-,-,0,%s,0,-,%s,-\n", n, ok ? "PASS" : "FAIL", d); fails += !ok; }

typedef struct { int frames_to_recover, presses, max_run, bad_bits, manual_lost; } RR;
static RR blowaway(UdeConfig *c, float hp, int recover_ok, int free_, const unsigned *phys, int nphys, int max_frames) {
    UdeState st; UdeSnapshot s; UdeDecision d; RR r; int f, mash = 0, prev_any = 0, run = 0;
    memset(&r, 0, sizeof r); r.frames_to_recover = -1; Ude_Reset(&st);
    for (f = 0; f < max_frames; f++) {
        unsigned ph = f < nphys ? phys[f] : 0, out; int any;
        memset(&s, 0, sizeof s); s.valid = s.fighting = 1; s.have_ext = 1; s.my_action = UDE_ACT_BLOW_AWAY; s.my_action_frame = f;
        s.opp_action = 0x0B; s.opp_skill_kind = -1; s.my_hp = (int)(hp * 30000); s.my_ki = 1; s.recover_ok = recover_ok; s.game_free = free_;
        s.flag5 = 1; { int k; for (k = 0; k < UDE_W_COUNT; k++) s.win[k] = -30; } s.blast_eta10 = -1; s.opp_attack_id = -1;
        out = Ude_Step(&st, c, &s, ph, &d);
        if ((out & ph) != ph || (out & ~ph & ~(UDE_B_GUARD | UDE_B_ANY_FACE))) r.bad_bits++;
        if (d.injected && (ph & ~(UDE_B_DIR_MASK | UDE_B_DERIVED))) r.manual_lost++;
        run = d.injected ? run + 1 : 0; if (run > r.max_run) r.max_run = run;
        any = (out & UDE_B_ANY_FACE) != 0;
        if (d.injected) r.presses += any && !prev_any;
        if (f > 8 && recover_ok && free_ && any && !prev_any && ++mash >= (int)((1.0f - hp) * 12.0f) + 4) { r.frames_to_recover = f; break; }
        prev_any = any;
    }
    return r;
}

int main(void) {
    UdeConfig c; RR r; char d[160]; unsigned manual[60]; int i;
    Ude_DefaultConfig(&c);
    r = blowaway(&c, 1.0f, 1, 1, NULL, 0, 120);
    emit("v4_recover_off_by_default", r.presses == 0 && r.frames_to_recover < 0, "ude_recover=0: no input while knocked away");
    c.recover = 1;
    r = blowaway(&c, 1.0f, 1, 1, NULL, 0, 120);
    snprintf(d, sizeof d, "full health: 4 presses needed; recovered at action frame %d; presses %d; longest injected run %d", r.frames_to_recover, r.presses, r.max_run);
    emit("v4_recover_full_health", r.frames_to_recover > 8 && r.frames_to_recover <= 17 && r.max_run == 1 && r.bad_bits == 0, d);
    r = blowaway(&c, 0.1f, 1, 1, NULL, 0, 120);
    snprintf(d, sizeof d, "10%% health: %d presses needed; recovered at frame %d", (int)(0.9f * 12) + 4, r.frames_to_recover);
    emit("v4_recover_low_health", r.frames_to_recover > 8 && r.frames_to_recover <= 9 + 2 * 15 && r.max_run == 1, d);
    r = blowaway(&c, 1.0f, 0, 1, NULL, 0, 60);
    emit("v4_recover_needs_character_bit", r.presses == 0, "parameter flags2 0x40 / flag 0x1D: no presses when the character cannot recover");
    r = blowaway(&c, 1.0f, 1, 0, NULL, 0, 60);
    emit("v4_recover_needs_free", r.presses == 0, "BtlChar_IsFree false: no presses");
    for (i = 0; i < 60; i++) manual[i] = (i >= 9 && i < 40) ? 0x8u : 0u;   /* the player presses RUSH from frame 9 to 39 */
    r = blowaway(&c, 1.0f, 1, 1, manual, 60, 60);
    emit("v4_recover_manual_wins", r.manual_lost == 0 && r.bad_bits == 0, "no injected press while the player presses; resumes after the grace");
    for (i = 0; i < 60; i++) manual[i] = 0x40u;                             /* the player holds a direction (game: recovery direction) */
    r = blowaway(&c, 1.0f, 1, 1, manual, 60, 60);
    emit("v4_recover_direction_held_is_players", r.presses == 0, "direction held (conditions 40/41 set the recovery flags): engine stays out");
    { UdeConfig off = c; UdeState st; UdeSnapshot s; UdeDecision dd; int k, inj = 0; off.enabled = 0; Ude_Reset(&st);
      for (k = 0; k < 40; k++) { memset(&s, 0, sizeof s); s.valid = s.fighting = s.have_ext = 1; s.my_action = UDE_ACT_BLOW_AWAY; s.my_action_frame = k;
          s.opp_action = 0x0B; s.recover_ok = s.game_free = 1; s.my_hp = 1; Ude_Step(&st, &off, &s, 0, &dd); inj += dd.injected; }
      emit("v4_recover_toggle_off", inj == 0, "F7 off: nothing"); }
    { UdeState st; UdeSnapshot s; UdeDecision dd; int k, inj = 0; Ude_Reset(&st);
      for (k = 0; k < 40; k++) { memset(&s, 0, sizeof s); s.valid = s.fighting = s.have_ext = 1; s.online = 1; s.my_action = UDE_ACT_BLOW_AWAY;
          s.my_action_frame = k; s.opp_action = 0x0B; s.recover_ok = s.game_free = 1; s.my_hp = 1; Ude_Step(&st, &c, &s, 0, &dd); inj += dd.injected; }
      emit("v4_recover_never_online", inj == 0, "online: suspended"); }
    return fails;
}
