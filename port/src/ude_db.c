/* UDE-DB1. Pure scalar input policy; all output is physical | added.
   Native eligibility: ude_db/AUDIT.md; never mutates a game flag/window/facing. */
#include "ude_db.h"
#include "ude_db_tab.h"

#define GUARD 0x1u
#define DASH 0x2u
#define BLAST 0x4u
#define RUSH 0x8u
#define UP 0x10u
#define DOWN 0x20u
#define LEFT 0x40u
#define RIGHT 0x80u
#define ANY 0x100000u
#define DERIVED (ANY | 0x78000000u | 0x01000000u)
#define AGE_MAX 1000000

void UdeDb_DefaultConfig(UdeDbConfig *c) { *c = kUdeDbDefaults; }
void UdeDb_Reset(UdeDbState *s) {
    unsigned i;
    unsigned char *p = (unsigned char *)s;
    for (i = 0; i < sizeof *s; i++) p[i] = 0;
    s->manual_age = s->press_age = AGE_MAX;
}
const UdeDbAttack *UdeDb_Attack(int ch, int attack) {
    return ch >= 0 && ch < UDE_DB_CHARACTER_COUNT && attack >= 0 && attack < UDE_DB_ATTACK_COUNT ? &kUdeDbAttacks[ch][attack] : 0;
}
const char *UdeDb_SourceSha256(void) { return UDE_DB_SOURCE_SHA256; }
const char *UdeDb_RecipeName(int k) { return k >= 0 && k < UDE_DB_RECIPE_COUNT ? kUdeDbTechniques[k].id : "INVALID"; }
const char *UdeDb_ReasonName(int r) {
    static const char *const extra[] = {
        "not_applicable", "first_hit_unknown", "later_hit", "capability", "animation", "record_unknown",
        "record_mismatch", "not_redirectable", "release", "press_gap", "pose_too_late", "unblockable", "sequence_gap", "forbidden", "lower_priority"
    };
    if (r >= 0 && r < UDE_R_COUNT) return Ude_ReasonName(r);
    return r >= UDE_R_COUNT && r < UDE_DB_R_COUNT ? extra[r - UDE_R_COUNT] : "invalid_reason";
}

static int recipe_value(const UdeDbSnapshot *s, int id, int unavailable) {
    return s->battle.have_recipe && s->battle.recipe[UDE_X_OK] ? s->battle.recipe[id] : unavailable;
}
static int incoming(const UdeDbSnapshot *s) { return recipe_value(s, UDE_X_INC, 0); }
static int first_hit(const UdeDbSnapshot *s) { return recipe_value(s, UDE_X_FIRST_HIT, -1); }
static int eta100(const UdeDbSnapshot *s, int threat) {
    int eta;
    if (threat == UDE_T_BLAST || threat == UDE_T_TECHNIQUE) {
        eta = recipe_value(s, UDE_X_INC_ETA100, UDE_X_NONE100);
        if ((incoming(s) & UDE_INC_HIT) && eta >= 0 && eta < UDE_X_NONE100) return eta;
        /* Do not confuse a cast's release time with an observed contact. */
        return -1;
    }
    eta = recipe_value(s, UDE_X_UNTIL100, UDE_X_NONE100);
    return eta >= 0 && eta < UDE_X_NONE100 ? eta : -1;
}

int UdeDb_Threat(const UdeDbSnapshot *s) {
    int a = s->battle.opp_action;
    int inc = incoming(s);
    int classified = a >= 0 && a < UDE_DB_ACTION_COUNT ? kUdeDbThreatByAction[a] : UDE_T_NONE;
    /* Never act on skills/prompts, even if a stale attack ID/projectile remains. */
    if (classified == UDE_T_SKILL || classified == UDE_T_CLASH) return classified;
    if (recipe_value(s, UDE_X_GRABBING, 0) || s->battle.opp_attack_id == 0x55 || (a >= 0xB4 && a <= 0xB6))
        return UDE_T_THROW;
    if (recipe_value(s, UDE_X_RUSHING, 0) || recipe_value(s, UDE_X_RUSHCAST, 0)) return UDE_T_RUSH;
    if (inc & UDE_INC_HIT) {
        if (inc & UDE_INC_SKILL) return UDE_T_SKILL;
        if (recipe_value(s, UDE_X_PROJ_FIRST, 0)) return inc & UDE_INC_TECH ? UDE_T_TECHNIQUE : UDE_T_BLAST;
    }
    if (classified == UDE_T_MELEE && !s->battle.opp_attacking && !recipe_value(s, UDE_X_MELEE, 0))
        return UDE_T_NONE;
    return classified;
}

static int behind(const UdeDbSnapshot *s, int threat) {
    if (threat == UDE_T_BLAST || threat == UDE_T_TECHNIQUE)
        return incoming(s) & UDE_INC_HIT ? !!(incoming(s) & UDE_INC_BEHIND) : s->battle.behind;
    return s->battle.behind;
}
static int ready(const UdeDbSnapshot *s, int window) {
    return window >= 0 && window < 6 && s->battle.win[window] <= kUdeDbWindows[window].floor;
}
static int all_guard_windows_ready(const UdeDbSnapshot *s) {
    return ready(s, UDE_W_EVADE) && ready(s, UDE_W_BLAST) && ready(s, UDE_W_THROW) && ready(s, UDE_W_RUSH);
}
static int fresh_input_reason(const UdeDbSnapshot *s, unsigned add, int pulse, int window) {
    int fresh_guard = (add & GUARD) && !(s->previous_added & GUARD);
    int fresh_side = (add & GUARD) && ((add & (LEFT|RIGHT)) & ~s->previous_added);
    unsigned trigger = add & RUSH ? (UP|RUSH) : add & BLAST ? 0x1000000u :
                       add & DASH ? DASH : (add & GUARD) && (add & (LEFT|RIGHT)) ? add & (LEFT|RIGHT) : GUARD;
    if (pulse && (add & GUARD) && !s->battle.flag5) return UDE_R_NOT_EVADABLE;
    if (pulse && window >= 0 && !ready(s, window)) return UDE_R_WINDOW_COOLING;
    /* A new plain hold is also condition 39. It cannot bypass anti-mash. */
    if (fresh_guard && s->battle.flag5 && !all_guard_windows_ready(s)) return UDE_R_WINDOW_COOLING;
    if ((fresh_guard || fresh_side || pulse) && s->press_age < (s->config.min_press_gap > 30 ? s->config.min_press_gap : 30))
        return UDE_DB_R_PRESS_GAP;
    if (pulse && (s->previous_added & trigger)) return UDE_R_RECIPE_EDGE;
    return UDE_R_NONE;
}
static int enabled(const UdeDbSnapshot *s, int k) {
    return kUdeDbTechniques[k].automatic && (s->config.kinds & (1u << k));
}
static int native_free(const UdeDbSnapshot *s) { return s->battle.have_ext && s->battle.game_free; }
static int timed(const UdeDbSnapshot *s, int threat) {
    int e = eta100(s, threat), lead = s->config.tap_lead100;
    if (e < 0) return UDE_R_WAIT_TIMING;
    /* One update of lead is the maximum safe default; larger values can be
       configured only up to the shortest native window (two updates). */
    if (lead < 0) lead = 0;
    if (lead > 100) lead = 100;
    return e <= lead ? UDE_R_NONE : UDE_R_WAIT_TIMING;
}
static int rank(int value) {
    return value == 4 ? 5 : value == 0 ? 4 : value == 1 ? 3 : value == 2 ? 2 : 1;
}
static int best_guard(const UdeDbAttack *a) {
    int i, best = 0;
    for (i = 1; i < 3; i++) if (rank(a->guard[i]) > rank(a->guard[best])) best = i;
    return best;
}
static int record_reason(const UdeDbSnapshot *s, const UdeDbAttack *a) {
    if (!a) return UDE_DB_R_RECORD_UNKNOWN;
    return a->flags == (unsigned)s->battle.opp_attack_flags ? UDE_R_NONE : UDE_DB_R_RECORD_MISMATCH;
}

/* Evaluate every recipe from scalar state. The priority table, packets, windows,
   capabilities, classes, defaults and record flags come from the generated DB. */
static int evaluate(const UdeDbSnapshot *s, int k, int t, unsigned *add) {
    const UdeSnapshot *b = &s->battle;
    const UdeDbAttack *a = UdeDb_Attack(s->opp_character, b->opp_attack_id);
    int caps = recipe_value(s, UDE_X_CAPS, 0);
    int pflags = recipe_value(s, UDE_X_PFLAGS, 0);
    int back = behind(s, t), first = first_hit(s), r, guard;
    int inc = incoming(s);
    *add = kUdeDbTechniques[k].buttons;
    if (!kUdeDbTechniques[k].automatic) return UDE_DB_R_FORBIDDEN;
    if (!enabled(s, k)) return UDE_R_DISABLED;
    if (k == UDE_DB_RECOVERY) {
        if (!s->config.recover) return UDE_R_DISABLED;
        if (b->my_action != UDE_ACT_BLOW_AWAY || b->my_action_frame <= 8 || !b->recover_ok) return UDE_DB_R_NOT_APPLICABLE;
        return native_free(s) ? UDE_R_NONE : UDE_R_BUSY;
    }
    if (b->my_action >= 0xB8 && b->my_action <= 0xEB) return UDE_R_BUSY;
    if (!native_free(s)) return UDE_R_BUSY;
    switch (k) {
    case UDE_DB_THROW_BREAK:
        if (t != UDE_T_THROW && !(t == UDE_T_RUSH && recipe_value(s, UDE_X_OPP_A5, 0))) return UDE_DB_R_NOT_APPLICABLE;
        if (back != 0) return back > 0 ? UDE_R_BEHIND : UDE_R_BAD_SNAPSHOT;
        if (!(b->anim_flags & 0x31) || (b->anim_flags & 0x800)) return UDE_DB_R_ANIMATION;
        if (!recipe_value(s, UDE_X_GRABBING, 0) && b->opp_attack_id != 0x55) return UDE_DB_R_NOT_APPLICABLE;
        r = timed(s, t); if (r) return r;
        return fresh_input_reason(s, *add, 1, recipe_value(s, UDE_X_OPP_A5, 0) ? UDE_W_RUSH : UDE_W_THROW);
    case UDE_DB_RUSH_EVADE:
        if (t != UDE_T_RUSH || recipe_value(s, UDE_X_OPP_A5, 0)) return UDE_DB_R_NOT_APPLICABLE;
        if (!(b->anim_flags & 0xB1) || b->no_evade) return UDE_DB_R_ANIMATION;
        if (recipe_value(s, UDE_X_OPP_UNK2, -1) < 0) return UDE_R_BAD_SNAPSHOT;
        r = timed(s, t); if (r) return r;
        return fresh_input_reason(s, *add, 1, UDE_W_RUSH);
    case UDE_DB_BLAST_EVADE:
        if (t != UDE_T_TECHNIQUE || !(inc & UDE_INC_EVADABLE)) return UDE_DB_R_NOT_APPLICABLE;
        if (s->projectile_hits < 0) return UDE_DB_R_FIRST_HIT_UNKNOWN;
        if (s->projectile_hits != 0) return UDE_DB_R_LATER_HIT;
        if (!(b->anim_flags & 0xB1) || b->no_evade) return UDE_DB_R_ANIMATION;
        r = timed(s, t); if (r) return r;
        return fresh_input_reason(s, *add, 1, UDE_W_BLAST);
    case UDE_DB_PERFECT_DEFLECT:
    case UDE_DB_KI_DEFLECT:
        if (t != UDE_T_BLAST) return UDE_DB_R_NOT_APPLICABLE;
        if (!(caps & 0x20)) return UDE_DB_R_CAPABILITY;
        if (!(inc & UDE_INC_REDIRECT)) return UDE_DB_R_NOT_REDIRECTABLE;
        if (back != 0) return back > 0 ? UDE_R_BEHIND : UDE_R_BAD_SNAPSHOT;
        if (k == UDE_DB_PERFECT_DEFLECT) {
            if (s->own_flag41 != 1 || (pflags & 2)) return UDE_DB_R_CAPABILITY;
            r = timed(s, t); if (r) return r;
            return fresh_input_reason(s, *add, 1, UDE_W_PERFECT);
        }
        /* Native stance admission requires an observed incoming blast <= 90.
           Keep GUARD+side held so condition 43 is not mashed every update. */
        if (eta100(s, t) < 0 || eta100(s, t) > 9000) return UDE_R_WAIT_TIMING;
        if ((s->previous_added & (GUARD|LEFT)) == (GUARD|LEFT) &&
            b->my_action != 0x3B && b->my_action != 0x3D) return UDE_R_RECIPE_EDGE;
        return fresh_input_reason(s, *add, 0, -1);
    case UDE_DB_VANISH_EVADE:
    case UDE_DB_SONIC_SWAY:
    case UDE_DB_Z_COUNTER:
        if (t != UDE_T_MELEE) return UDE_DB_R_NOT_APPLICABLE;
        if (first < 0) return UDE_DB_R_FIRST_HIT_UNKNOWN;
        if (!first) return UDE_DB_R_LATER_HIT;
        r = record_reason(s, a); if (r) return r;
        if (k == UDE_DB_SONIC_SWAY) {
            if (!(caps & 0x80) || b->opp_attack_id >= 0xB || recipe_value(s,UDE_X_OPP_UNK2,4) == 4) return UDE_DB_R_CAPABILITY;
            if (b->my_ki <= 0) return UDE_R_NO_KI;
            if (back != 0) return back > 0 ? UDE_R_BEHIND : UDE_R_BAD_SNAPSHOT;
            if (b->no_evade) return UDE_DB_R_ANIMATION;
        } else if (k == UDE_DB_VANISH_EVADE) {
            if (!(a->flags & 1)) return UDE_R_NOT_EVADABLE;
            if (b->no_evade || !recipe_value(s,UDE_X_DODGE_WIN,0)) return UDE_DB_R_ANIMATION;
        } else {
            if (!s->config.z_counter) return UDE_R_DISABLED;
            if (!(b->anim_flags & 0x31)) return UDE_DB_R_ANIMATION;
        }
        r = timed(s, t); if (r) return r;
        r = fresh_input_reason(s,*add,1,k == UDE_DB_VANISH_EVADE ? UDE_W_EVADE : k == UDE_DB_Z_COUNTER ? UDE_W_COUNTER : -1);
        if (r) return r;
        /* A Z window lasts one update. At low health an extra-level melee
           evasion has only one usable update too. Release preparation may
           happen one update earlier, but the press itself must be at contact. */
        if ((k == UDE_DB_Z_COUNTER || (k == UDE_DB_VANISH_EVADE && (a->flags & 0x4000000))) && eta100(s,t) != 0)
            return UDE_R_WAIT_TIMING;
        return UDE_R_NONE;
    case UDE_DB_DRAGON_DASH_ESCAPE:
    case UDE_DB_SIDESTEP:
        if (t != UDE_T_TECHNIQUE && t != UDE_T_BLAST) return UDE_DB_R_NOT_APPLICABLE;
        if (k == UDE_DB_DRAGON_DASH_ESCAPE) {
            if (!s->config.dragon_escape) return UDE_R_DISABLED;
            if (recipe_value(s,UDE_X_DIST100,0) < 18000) return UDE_R_OUT_OF_REACH;
            if (b->my_ki < 1000) return UDE_R_NO_KI;
        } else {
            if (!s->config.sidestep) return UDE_R_DISABLED;
            if (!b->close) return UDE_R_OUT_OF_REACH;
        }
        if (!b->flag5) return UDE_R_NOT_EVADABLE;
        return fresh_input_reason(s,*add,1,-1);
    case UDE_DB_GUARD_HIGH_LOW:
    case UDE_DB_GUARD_HOLD:
        if (t != UDE_T_MELEE && t != UDE_T_RUSH && t != UDE_T_TECHNIQUE && t != UDE_T_BLAST) return UDE_DB_R_NOT_APPLICABLE;
        if (back != 0) return back > 0 ? UDE_R_BEHIND : UDE_R_BAD_SNAPSHOT;
        if (t == UDE_T_MELEE) {
            r = record_reason(s,a); if (r) return r;
            guard = best_guard(a);
            if (a->guard[guard] == 3) return UDE_DB_R_UNBLOCKABLE;
            if (guard != 0) {
                int desired = guard == 1 ? 0xF1 : 0xF3;
                if (k != UDE_DB_GUARD_HIGH_LOW) return UDE_DB_R_NOT_APPLICABLE;
                if (b->my_action == 0x38 && s->own_motion != desired && eta100(s,t) < 300) return UDE_DB_R_POSE_TOO_LATE;
                *add |= guard == 1 ? UP : DOWN;
            } else if (k != UDE_DB_GUARD_HOLD) return UDE_DB_R_NOT_APPLICABLE;
        } else if (k != UDE_DB_GUARD_HOLD) return UDE_DB_R_NOT_APPLICABLE;
        if ((t == UDE_T_TECHNIQUE || t == UDE_T_BLAST) && !(inc & UDE_INC_HIT)) return UDE_R_NO_THREAT;
        if (t == UDE_T_MELEE && !b->close && !recipe_value(s,UDE_X_D_LT_R,0)) return UDE_R_OUT_OF_REACH;
        return fresh_input_reason(s,*add,0,-1);
    default: return UDE_DB_R_FORBIDDEN;
    }
}

static UdeDbDecision blocked(const UdeDbSnapshot *s, int reason) {
    UdeDbDecision d;
    int i;
    d.recipe = UDE_DB_NONE; d.reason = reason; d.threat = UDE_T_NONE;
    d.press_frame = -1; d.added = 0; d.buttons = s ? s->physical : 0; d.press_edge = 0;
    for (i = 0; i < UDE_DB_RECIPE_COUNT; i++) d.rejected[i] = reason;
    return d;
}

UdeDbDecision UdeDb_Choose(const UdeDbSnapshot *s) {
    UdeDbDecision d;
    int i, r, t, chosen = 0, preparing_release = 0, primary = UDE_R_NO_THREAT;
    unsigned add;
    if (!s || !s->config.enabled) return blocked(s,UDE_R_DISABLED);
    if (!s->battle.valid || !s->battle.have_ext || !s->battle.have_recipe) return blocked(s,UDE_R_BAD_SNAPSHOT);
    if (s->battle.my_action < 0 || s->battle.my_action >= UDE_DB_ACTION_COUNT ||
        s->battle.opp_action < 0 || s->battle.opp_action >= UDE_DB_ACTION_COUNT) return blocked(s,UDE_R_BAD_SNAPSHOT);
    if (s->battle.online) return blocked(s,UDE_R_ONLINE);
    if (s->battle.replay) return blocked(s,UDE_R_REPLAY);
    if (s->battle.cpu_side) return blocked(s,UDE_R_CPU_SIDE);
    if (s->battle.time_stopped || recipe_value(s,UDE_X_STOPPED,0)) return blocked(s,UDE_R_TIME_STOP);
    if (!s->battle.fighting) return blocked(s,UDE_R_NOT_FIGHTING);
    if ((s->physical & ~DERIVED) || s->manual_age < (s->config.manual_grace < 0 ? 0 : s->config.manual_grace)) return blocked(s,UDE_R_MANUAL);
    if (s->manual_claim) return blocked(s,UDE_R_MANUAL_ACTION);
    if (s->battle.my_request_pending) return blocked(s,UDE_R_REQUEST_PENDING);
    if (s->release_due) return blocked(s,UDE_DB_R_RELEASE);
    if (s->battle.my_ki < s->config.min_ki) return blocked(s,UDE_R_NO_KI);
    if (recipe_value(s,UDE_X_MY_TECH,0)) return blocked(s,UDE_R_BUSY);
    if (s->holding >= (s->config.hold_cap > 0 ? s->config.hold_cap : 90)) return blocked(s,UDE_R_HOLD_CAP);
    t = UdeDb_Threat(s);
    if (t == UDE_T_SKILL || t == UDE_T_CLASH || (s->battle.my_action >= 0xFA && s->battle.my_action <= 0x102) ||
        (s->battle.my_action >= 0x130 && s->battle.my_action <= 0x132) ||
        (s->battle.my_action >= 0xEC && s->battle.my_action <= 0xF3)) return blocked(s,UDE_DB_R_FORBIDDEN);
    d = blocked(s,UDE_DB_R_NOT_APPLICABLE); d.threat = t;
    for (i = 1; i < UDE_DB_RECIPE_COUNT; i++) if (!kUdeDbTechniques[i].automatic) d.rejected[i] = UDE_DB_R_FORBIDDEN;
    for (i = 0; i < (int)(sizeof kUdeDbPriority / sizeof kUdeDbPriority[0]); i++) {
        int k = kUdeDbPriority[i];
        r = evaluate(s,k,t,&add);
        if (!r) r = fresh_input_reason(s,add,kUdeDbTechniques[k].pulse,-1);
        d.rejected[k] = !r && (chosen || preparing_release) ? UDE_DB_R_LOWER_PRIORITY : r;
        if (!chosen && !preparing_release && r == UDE_R_RECIPE_EDGE &&
            ((eta100(s,t) >= 100 && eta100(s,t) <= 9000) || k == UDE_DB_RECOVERY) &&
            s->press_age >= (s->config.min_press_gap > 30 ? s->config.min_press_gap : 30) &&
            all_guard_windows_ready(s)) {
            preparing_release = 1; d.reason = UDE_DB_R_RELEASE;
            d.press_frame = (int)(s->frame + 1u);
        }
        if (r != UDE_DB_R_NOT_APPLICABLE && r != UDE_R_DISABLED && primary == UDE_R_NO_THREAT) primary = r;
        if (!r && !chosen && !preparing_release) {
            chosen = k; d.recipe = k; d.added = add;
            d.reason = UDE_R_NONE; d.press_frame = (int)s->frame;
            d.press_edge = kUdeDbTechniques[k].pulse || ((add & GUARD) && !(s->previous_added & GUARD)) ||
                           ((add & GUARD) && ((add & (LEFT|RIGHT)) & ~s->previous_added));
        }
    }
    if (!chosen && !preparing_release) d.reason = primary;
    d.buttons = s->physical | d.added;
    return d;
}

unsigned UdeDb_Step(UdeDbState *state, const UdeDbConfig *c, const UdeDbSnapshot *raw,
                    unsigned physical, unsigned frame, UdeDbDecision *out) {
    UdeDbSnapshot s;
    UdeDbDecision d;
    int manual = (physical & ~DERIVED) != 0;
    if (!state || !c || !raw) { d = blocked(0,UDE_R_BAD_SNAPSHOT); d.buttons = physical; if (out) *out = d; return physical; }
    s = *raw; s.config = *c; s.physical = physical; s.frame = frame;
    if (state->have_frame && frame != state->last_frame + 1u) {
        /* A sparse trace/load cannot prove an input edge. Wait >=30 updates. */
        UdeDb_Reset(state); state->press_age = 0;
    }
    if (state->manual_age < AGE_MAX) state->manual_age++;
    if (state->press_age < AGE_MAX) state->press_age++;
    if (manual) { state->manual_age = 0; state->claim = 1; state->claim_wait = 2; }
    if (state->claim && !manual) {
        int a = raw->battle.my_action;
        int neutral = a == 0xB || a == 0xD || a == 0xE || a == 0xF || a == 0x38 || (a >= 0xB8 && a <= 0xEB);
        if (state->claim_wait > 0) state->claim_wait--;
        else if (neutral) state->claim = 0;
    }
    s.manual_age = state->manual_age; s.press_age = state->press_age;
    s.manual_claim = state->claim; s.release_due = state->release_due;
    s.previous_added = state->previous_added; s.holding = state->holding;
    d = UdeDb_Choose(&s);
    if ((d.buttons & physical) != physical) { state->manual_overwrites++; d.buttons |= physical; }
    state->release_due = d.recipe > 0 && kUdeDbTechniques[d.recipe].pulse;
    state->previous_added = d.added;
    state->holding = d.added & GUARD ? state->holding + 1 : 0;
    if (d.press_edge) { state->press_age = 0; state->interventions++; if (kUdeDbTechniques[d.recipe].pulse) state->taps++; }
    state->frames++; state->last_frame = frame; state->have_frame = 1;
    if (out) *out = d;
    return d.buttons;
}
