/* BT3 Universal Defense Engine: the decision core. See ude.h. No allocation, no I/O, O(1) per frame. */
#include "ude.h"
#include "ude_policy_tab.h"

static const char *const kReason[UDE_R_COUNT] = {
    "none", "disabled", "online", "replay", "cpu_side", "not_fighting", "time_stop", "bad_snapshot", "no_threat",
    "manual", "moving", "busy", "out_of_reach", "behind", "no_ki", "hold_cap", "not_accepted", "cooldown"
};
static const char *const kThreat[7] = { "none", "melee", "rush", "technique", "skill", "clash", "blast" };

const char *Ude_ReasonName(int r) { return (r >= 0 && r < UDE_R_COUNT) ? kReason[r] : "?"; }
const char *Ude_ThreatName(int t) { return (t >= 0 && t < 7) ? kThreat[t] : "?"; }

void Ude_DefaultConfig(UdeConfig *c) {
    c->enabled = 1;
    c->guard_melee = c->guard_rush = c->guard_technique = c->guard_blast = 1;
    c->manual_grace = 4;
    c->allow_while_moving = 0;
    c->hold_tail = 3;
    c->hold_cap = 90;
    c->cooldown = 20;
    c->accept_frames = 8;
    c->suspend_frames = 60;
    c->min_ki = 0;
    c->skip_behind = 1;
}

void Ude_Reset(UdeState *s) {
    unsigned char *p = (unsigned char *)s;
    unsigned i;
    for (i = 0; i < sizeof(*s); i++) p[i] = 0;
    s->last_manual = 1000;
}

static int in_range(int a) { return a >= 0 && a < UDE_ACTION_COUNT; }

/* Neutral, movement and guard families (generated from the database): the player is free to start a guard. */
int Ude_IsFreeAction(int a) { return in_range(a) && kUdeFreeAction[a]; }
/* Guard and its reactions: action ids 0x36..0x46 (docs/systems/combat.md, "Action id families"). */
int Ude_IsGuardAction(int a) { return a >= 0x36 && a <= 0x46; }
static int is_reaction(int a) { return a >= 0xB8 && a <= 0xEB; }

int Ude_ClassifyThreat(const UdeConfig *c, const UdeSnapshot *s) {
    int t = in_range(s->opp_action) ? kUdeThreatByAction[s->opp_action] : UDE_T_NONE;

    switch (t) {
    case UDE_T_MELEE:
        /* an attack action is a threat only within reach (flag 0x13) and while the AI's own test says it attacks */
        if (!c->guard_melee) return UDE_T_NONE;
        if (!s->close) return -UDE_R_OUT_OF_REACH;
        return UDE_T_MELEE;
    case UDE_T_RUSH:
        if (!c->guard_rush) return UDE_T_NONE;
        return s->close ? UDE_T_RUSH : -UDE_R_OUT_OF_REACH;
    case UDE_T_TECHNIQUE:
        /* the AI's BtlAiStep_GuardUntilSafe treats skill kinds 1..2 as "guard needed"; beams also show as blasts */
        if (!c->guard_technique) return UDE_T_NONE;
        if (s->opp_skill_kind == 1 || s->opp_skill_kind == 2 || s->incoming_blast || s->close) return UDE_T_TECHNIQUE;
        return UDE_T_NONE;
    case UDE_T_CLASH:
    case UDE_T_SKILL:
        return UDE_T_NONE;   /* no defence injected by design */
    default:
        break;
    }
    if (s->incoming_blast && c->guard_blast) return UDE_T_BLAST;
    /* the opponent's attack flag without a classified action (e.g. an action id the extractor did not classify) */
    if (s->opp_attacking && s->close && c->guard_melee) return UDE_T_MELEE;
    return UDE_T_NONE;
}

static unsigned finish(UdeState *s, UdeDecision *out, unsigned buttons, int injected, int threat, int reason) {
    s->last_reason = injected ? UDE_R_NONE : reason;
    s->last_threat = threat;
    if (injected) {
        s->stats.injected_frames++;
    } else if (reason >= 0 && reason < UDE_R_COUNT) {
        s->stats.reasons[reason]++;
    }
    if (out) {
        out->buttons = buttons;
        out->injected = injected;
        out->threat = threat;
        out->reason = injected ? UDE_R_NONE : reason;
    }
    return buttons;
}

static unsigned release(UdeState *s, UdeDecision *out, unsigned physical, int threat, int reason) {
    s->holding = 0;
    s->tail = 0;
    return finish(s, out, physical, 0, threat, reason);
}

unsigned Ude_Step(UdeState *s, const UdeConfig *c, const UdeSnapshot *snap, unsigned physical, UdeDecision *out) {
    unsigned manual = physical & ~(UDE_B_DIR_MASK | UDE_B_DERIVED);
    int threat;

    s->stats.frames++;
    if (s->last_manual < 1000) s->last_manual++;
    if (manual) {
        if (s->holding) s->stats.manual_overrides++;
        s->last_manual = 0;
    }

    /* ---- Safety controller: situations in which the engine never acts */
    if (!c->enabled) { s->mode = UDE_OFF; return release(s, out, physical, 0, UDE_R_DISABLED); }
    if (snap == 0 || !snap->valid) { s->mode = UDE_SUSPENDED; return release(s, out, physical, 0, UDE_R_BAD_SNAPSHOT); }
    if (snap->online) { s->mode = UDE_SUSPENDED; return release(s, out, physical, 0, UDE_R_ONLINE); }
    if (snap->replay) { s->mode = UDE_SUSPENDED; return release(s, out, physical, 0, UDE_R_REPLAY); }
    if (snap->cpu_side) { s->mode = UDE_SUSPENDED; return release(s, out, physical, 0, UDE_R_CPU_SIDE); }
    if (!snap->fighting) { s->mode = UDE_ON; return release(s, out, physical, 0, UDE_R_NOT_FIGHTING); }
    if (snap->time_stopped) { s->mode = UDE_ON; return release(s, out, physical, 0, UDE_R_TIME_STOP); }
    if (!in_range(snap->my_action) || !in_range(snap->opp_action)) {
        s->mode = UDE_SUSPENDED; return release(s, out, physical, 0, UDE_R_BAD_SNAPSHOT);
    }
    if (s->suspend > 0) { s->suspend--; s->mode = UDE_SUSPENDED; return release(s, out, physical, 0, UDE_R_NOT_ACCEPTED); }
    s->mode = UDE_ON;

    /* ---- Threat classifier */
    threat = Ude_ClassifyThreat(c, snap);
    if (threat > 0 && threat < 7) s->stats.threats[threat]++;

    /* ---- Input arbiter: manual input always wins, and the engine waits a few frames after it */
    if (manual || s->last_manual < c->manual_grace) {
        return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_MANUAL);
    }
    if (physical & UDE_B_GUARD) {  /* the player guards already (cannot happen: GUARD is manual; kept for safety) */
        return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_MANUAL);
    }
    if (!c->allow_while_moving && (physical & UDE_B_DIR_MASK)) {
        return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_MOVING);
    }
    if (s->cooldown > 0) { s->cooldown--; return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_COOLDOWN); }

    /* ---- Defense policy */
    if (threat <= 0) {
        if (s->holding && s->tail > 0) {          /* Timing: keep the guard a few frames after the threat ends */
            s->tail--;
            s->holding++;
            return finish(s, out, physical | UDE_B_GUARD, 1, 0, UDE_R_NONE);
        }
        return release(s, out, physical, 0, threat < 0 ? -threat : UDE_R_NO_THREAT);
    }
    if (c->skip_behind && snap->behind == 1) return release(s, out, physical, threat, UDE_R_BEHIND);
    if (snap->my_ki < c->min_ki) return release(s, out, physical, threat, UDE_R_NO_KI);
    if (!Ude_IsFreeAction(snap->my_action)) {     /* attacking, in a reaction, transforming...: not the engine's call */
        return release(s, out, physical, threat, UDE_R_BUSY);
    }

    /* ---- Timing controller */
    if (s->holding >= c->hold_cap) {              /* never hold a button indefinitely */
        s->cooldown = c->cooldown;
        return release(s, out, physical, threat, UDE_R_HOLD_CAP);
    }
    if (s->holding >= c->accept_frames && !Ude_IsGuardAction(snap->my_action) && !is_reaction(snap->my_action)) {
        /* guard was sent for accept_frames but the fighter never entered a guard action: the game refuses it */
        if (++s->not_accepted >= 2) {
            s->suspend = c->suspend_frames; s->stats.suspends++; s->not_accepted = 0; s->mode = UDE_SUSPENDED;
        }
        s->cooldown = c->cooldown;
        return release(s, out, physical, threat, UDE_R_NOT_ACCEPTED);
    }
    if (Ude_IsGuardAction(snap->my_action)) s->not_accepted = 0;
    if (s->holding == 0) s->stats.interventions++;
    s->holding++;
    s->tail = c->hold_tail;
    return finish(s, out, physical | UDE_B_GUARD, 1, threat, UDE_R_NONE);
}
