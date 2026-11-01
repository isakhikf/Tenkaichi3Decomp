/* BT3 Universal Defense Engine: the decision core. See ude.h. No allocation, no I/O, O(1) per frame.
 *
 * v2 (cycle 2): besides holding guard (v1), the engine can send a single, timed guard press (TAP). In the game a fresh
 * guard press with fighter flag 5 re-opens the defence windows of BtlMove_UpdateDefenseTimers, and those windows are
 * what BtlHit_CheckDodge (melee evasion, no behind test), BtlHit_CheckThrowBreak (throws, from the front) and the blast
 * side step in btl_char_coll_b.c read. A TAP is only sent where holding guard is proven useless (from behind, throws)
 * and only when the window it needs is at its floor, because a press during the cooldown closes it (-1).
 */
#include "ude.h"
#include "ude_policy_tab.h"

static const char *const kReason[UDE_R_COUNT] = {
    "none", "disabled", "online", "replay", "cpu_side", "not_fighting", "time_stop", "bad_snapshot", "no_threat",
    "manual", "moving", "busy", "out_of_reach", "behind", "no_ki", "hold_cap", "not_accepted", "cooldown",
    "request_pending", "wait_timing", "window_cooling", "not_evadable"
};
static const char *const kThreat[UDE_T_COUNT] = { "none", "melee", "rush", "technique", "skill", "clash", "blast", "throw" };
static const char *const kResp[5] = { "none", "hold", "tap", "press_hold", "recover" };

const char *Ude_ReasonName(int r) { return (r >= 0 && r < UDE_R_COUNT) ? kReason[r] : "?"; }
const char *Ude_ThreatName(int t) { return (t >= 0 && t < UDE_T_COUNT) ? kThreat[t] : "?"; }
const char *Ude_ResponseName(int r) { return (r >= 0 && r < 5) ? kResp[r] : "?"; }

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
    c->tap_defense = 1;
    c->front_timing = 0;
    c->tap_lead = 1;
    c->request_guard = 1;
    c->recover = 0;
}

void Ude_Reset(UdeState *s) {
    unsigned char *p = (unsigned char *)s;
    unsigned i;
    for (i = 0; i < sizeof(*s); i++) p[i] = 0;
    s->last_manual = 1000;
    s->since_press = 1000;
}

static int in_range(int a) { return a >= 0 && a < UDE_ACTION_COUNT; }

int Ude_IsFreeAction(int a) { return in_range(a) && kUdeFreeAction[a]; }
int Ude_IsGuardAction(int a) { return a >= 0x36 && a <= 0x46; }
static int is_reaction(int a) { return a >= 0xB8 && a <= 0xEB; }
static int is_side_step(int a) { return a >= 0x20 && a <= 0x22; }  /* forced from flags 0x74..0x76 */

int Ude_ClassifyThreat(const UdeConfig *c, const UdeSnapshot *s) {
    int t = in_range(s->opp_action) ? kUdeThreatByAction[s->opp_action] : UDE_T_NONE;

    if (s->have_ext && s->opp_attack_id == 0x55 && s->close) return UDE_T_THROW;
    switch (t) {
    case UDE_T_MELEE:
        if (!c->guard_melee) return UDE_T_NONE;
        if (!s->close) return -UDE_R_OUT_OF_REACH;
        return UDE_T_MELEE;
    case UDE_T_RUSH:
        if (!c->guard_rush) return UDE_T_NONE;
        return s->close ? UDE_T_RUSH : -UDE_R_OUT_OF_REACH;
    case UDE_T_TECHNIQUE:
        if (!c->guard_technique) return UDE_T_NONE;
        /* kinds 1..2: BtlAiStep_GuardUntilSafe's "guard needed"; kind 3 (cycle 5, real log ude_events.csv session 1):
           action 0x106 hit 4 times from ~380 units with no blast record seen by FindIncomingBlast (a ranged beam) */
        if (s->opp_skill_kind == 1 || s->opp_skill_kind == 2 || s->opp_skill_kind == 3 || s->incoming_blast || s->close)
            return UDE_T_TECHNIQUE;
        return UDE_T_NONE;
    case UDE_T_CLASH:
    case UDE_T_SKILL:
        return UDE_T_NONE;
    default:
        break;
    }
    if (s->incoming_blast && c->guard_blast) return UDE_T_BLAST;
    if (s->opp_attacking && s->close && c->guard_melee) return UDE_T_MELEE;
    return UDE_T_NONE;
}

static int confidence(const UdeSnapshot *s, int threat) {
    int classified = in_range(s->opp_action) && kUdeThreatByAction[s->opp_action] != 0;
    if (threat <= 0) return UDE_C_NONE;
    if (s->have_ext && s->opp_hit_active && threat != UDE_T_BLAST) return UDE_C_CONFIRMED;
    if (threat == UDE_T_BLAST && s->have_ext && s->blast_eta10 >= 0 && s->blast_eta10 <= 50) return UDE_C_CONFIRMED;
    if (threat == UDE_T_BLAST || classified || threat == UDE_T_THROW) return UDE_C_PROBABLE;
    return UDE_C_UNKNOWN;     /* only the opponent's attack flag says so */
}

/* Frames until contact when known, -1 when not. Blasts: the game-side geometric estimate. Melee / throws: the measured
   table (kUdeEtaByAction, from import_log.py; 0 = not measured). "Hit volumes active" is NOT used as a timing source:
   the hook samples before this frame's action update, so it shows the previous frame, whose hit test (the first hit,
   the only one BtlHit_CheckDodge evades) has already run (order: src/battle/btl_char_mgr.c, steps 2 -> 4). */
static unsigned char sEta[UDE_ACTION_COUNT];
static int sEtaInit;
static void eta_init(void) {
    int i;
    for (i = 0; i < UDE_ACTION_COUNT; i++) sEta[i] = kUdeEtaByAction[i];
    sEtaInit = 1;
}
/* Measured timing loaded at run time (ude_eta.txt, written by import_log.py), over the compiled-in table. */
void Ude_SetEta(int action, int frames) {
    if (!sEtaInit) eta_init();
    if (in_range(action)) sEta[action] = (unsigned char)(frames < 0 ? 0 : frames > 255 ? 255 : frames);
}
void Ude_ClearEta(void) { eta_init(); }

int Ude_EtaFrames(const UdeSnapshot *s, int threat) {
    if (!sEtaInit) eta_init();
    if (!s->have_ext) return -1;
    if (threat == UDE_T_BLAST || (threat == UDE_T_TECHNIQUE && s->blast_eta10 >= 0)) {
        return s->blast_eta10 >= 0 ? s->blast_eta10 / 10 : -1;
    }
    if (in_range(s->opp_action) && sEta[s->opp_action] > 0) {
        int e = sEta[s->opp_action] - s->opp_action_frame;
        return e >= 0 ? e : -1;
    }
    return -1;
}

static unsigned finish(UdeState *s, UdeDecision *out, unsigned buttons, int injected, int threat, int reason,
                       int resp, int conf) {
    int edge = injected && !s->last_injected;
    s->last_reason = injected ? UDE_R_NONE : reason;
    s->last_threat = threat;
    if (edge) s->since_press = 0;
    s->last_injected = injected;
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
        out->response = resp;
        out->confidence = conf;
        out->press_edge = edge;
    }
    return buttons;
}

static unsigned release(UdeState *s, UdeDecision *out, unsigned physical, int threat, int reason) {
    s->holding = 0;
    s->tail = 0;
    s->tap_left = 0;
    return finish(s, out, physical, 0, threat, reason, UDE_RESP_NONE, UDE_C_NONE);
}

/* A press may start only if the window it needs is at its floor (otherwise the press closes it) and the engine's
   own last press is older than the cooldown (also when the window cannot be read). */
static int press_allowed(const UdeState *s, const UdeSnapshot *n, int win) {
    if (n->have_ext && !n->flag5) return 0;            /* without flag 5 the press opens nothing */
    if (n->have_ext && n->win[win] > UDE_W_FLOOR) return 0;
    return s->since_press > 31;
}

unsigned Ude_Step(UdeState *s, const UdeConfig *c, const UdeSnapshot *snap, unsigned physical, UdeDecision *out) {
    unsigned manual = physical & ~(UDE_B_DIR_MASK | UDE_B_DERIVED);
    int threat, conf, eta, behind;

    s->stats.frames++;
    if (s->last_manual < 1000) s->last_manual++;
    if (s->since_press < 1000) s->since_press++;
    if (manual) {
        if (s->holding || s->tap_left) s->stats.manual_overrides++;
        s->last_manual = 0;
    }

    /* ---- Safety controller */
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
    if (threat > 0 && threat < UDE_T_COUNT) s->stats.threats[threat]++;
    conf = confidence(snap, threat);

    /* ---- Input arbiter */
    if (manual || s->last_manual < c->manual_grace) {
        return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_MANUAL);
    }
    if (physical & UDE_B_GUARD) return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_MANUAL);
    if (!c->allow_while_moving && (physical & UDE_B_DIR_MASK)) {
        return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_MOVING);
    }
    /* the player's own request is queued for the next dispatch: a guard now could replace it (BtlAct_CheckForced
       / decide requests overwrite each other) */
    if (c->request_guard && snap->have_ext && snap->my_request_pending && !s->holding && !s->tap_left) {
        return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_REQUEST_PENDING);
    }
    if (s->cooldown > 0) { s->cooldown--; return release(s, out, physical, threat > 0 ? threat : 0, UDE_R_COOLDOWN); }

    /* ---- knock-away recovery (BtlAct_BlowAwayHandler, src/battle/btl_act_a.c): free, more than 8 frames into 0xD5, the
       character able to recover: every NEW face press (battle bit ANY_FACE) counts; at (1 - health) x 12 + 4 presses the
       game switches to 0xD7. A press on alternate frames, like the CPU's virtual pad. No threat needed: it is a state. */
    if (c->recover && snap->have_ext && snap->my_action == UDE_ACT_BLOW_AWAY && snap->my_action_frame > 8 && snap->recover_ok &&
        snap->game_free) {
        s->holding = 0;
        s->tail = 0;
        s->tap_left = 0;
        s->recover_phase = !s->recover_phase;
        if (s->recover_phase) {
            s->stats.recover_presses++;
            return finish(s, out, physical | UDE_B_GUARD | UDE_B_ANY_FACE, 1, threat > 0 ? threat : 0, UDE_R_NONE, UDE_RESP_RECOVER, conf);
        }
        return finish(s, out, physical, 0, threat > 0 ? threat : 0, UDE_R_NONE, UDE_RESP_RECOVER, conf);
    }
    s->recover_phase = 0;

    /* ---- a tap in progress: send it, then release (exactly one press) */
    if (s->tap_left > 0) {
        s->tap_left--;
        return finish(s, out, physical | UDE_B_GUARD, 1, threat > 0 ? threat : 0, UDE_R_NONE, UDE_RESP_TAP, conf);
    }

    /* ---- Defense policy */
    if (threat <= 0) {
        if (s->holding && s->tail > 0) {
            s->tail--;
            s->holding++;
            return finish(s, out, physical | UDE_B_GUARD, 1, 0, UDE_R_NONE, UDE_RESP_HOLD, UDE_C_NONE);
        }
        return release(s, out, physical, 0, threat < 0 ? -threat : UDE_R_NO_THREAT);
    }
    if (snap->my_ki < c->min_ki) return release(s, out, physical, threat, UDE_R_NO_KI);
    if (!Ude_IsFreeAction(snap->my_action)) return release(s, out, physical, threat, UDE_R_BUSY);

    eta = Ude_EtaFrames(snap, threat);
    behind = snap->behind == 1;

    /* Where guard is proven useless, a timed press is the only defence the code offers. */
    if (threat == UDE_T_THROW || (behind && (threat == UDE_T_MELEE || threat == UDE_T_BLAST))) {
        int win = threat == UDE_T_THROW ? UDE_W_THROW : threat == UDE_T_BLAST ? UDE_W_BLAST : UDE_W_EVADE;
        int lead = threat == UDE_T_THROW ? 4 : c->tap_lead;     /* the throw window lasts 5 frames */
        if (!c->tap_defense || !snap->have_ext) {
            s->stats.impossible++;
            return release(s, out, physical, threat, behind ? UDE_R_BEHIND : UDE_R_NOT_EVADABLE);
        }
        if (threat == UDE_T_THROW && behind) {                    /* the break needs the defender facing the attacker */
            s->stats.impossible++;
            return release(s, out, physical, threat, UDE_R_NOT_EVADABLE);
        }
        if (threat == UDE_T_MELEE && !(snap->opp_attack_flags & 1)) {   /* CheckDodge: only flag-1 attacks */
            s->stats.impossible++;
            return release(s, out, physical, threat, UDE_R_NOT_EVADABLE);
        }
        if (snap->no_evade || !(snap->anim_flags & (threat == UDE_T_THROW ? 0x31 : 0xB1))) {
            s->stats.impossible++;
            return release(s, out, physical, threat, UDE_R_NOT_EVADABLE);
        }
        if (eta < 0 || eta > lead) return release(s, out, physical, threat, UDE_R_WAIT_TIMING);
        if (!press_allowed(s, snap, win)) return release(s, out, physical, threat, UDE_R_WINDOW_COOLING);
        s->holding = 0;
        s->tail = 0;
        s->tap_left = 0;                 /* one frame: this one */
        s->stats.taps++;
        s->stats.interventions++;
        return finish(s, out, physical | UDE_B_GUARD, 1, threat, UDE_R_NONE, UDE_RESP_TAP, conf);
    }
    if (behind && c->skip_behind) {      /* rush / technique from behind: no defence found in the code */
        s->stats.impossible++;
        return release(s, out, physical, threat, UDE_R_BEHIND);
    }

    /* Front: guard (v1). With front_timing the first press is delayed to the contact so that the windows (dodge,
       blast side step, rush evasion) are open when the hit is tested; guard stays held after it. */
    if (c->front_timing && !s->holding && snap->have_ext && eta >= 0 && eta > c->tap_lead) {
        return release(s, out, physical, threat, UDE_R_WAIT_TIMING);
    }

    /* ---- Timing controller */
    if (s->holding >= c->hold_cap) {
        s->cooldown = c->cooldown;
        return release(s, out, physical, threat, UDE_R_HOLD_CAP);
    }
    if (s->holding >= c->accept_frames && !Ude_IsGuardAction(snap->my_action) && !is_reaction(snap->my_action) &&
        !is_side_step(snap->my_action)) {
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
    return finish(s, out, physical | UDE_B_GUARD, 1, threat, UDE_R_NONE,
                  (c->front_timing && s->holding == 1 && eta >= 0) ? UDE_RESP_PRESS_HOLD : UDE_RESP_HOLD, conf);
}
