/* BT3 UDE Hybrid v5: Codex R8 input recipes, off-line. See ude_recipe.h.
 *
 * This is the decision part of Codex R8 OnlineDefense.c online_defense (sha256 of the file: 55c46838...), in the same
 * order, with the values of its deployed configuration (DefenseOptions {63, kinds, 1}, OnlineOptions {7, 6, 24},
 * DefensePriority 3, Config[5] = 0) and the off-line timing (pad delay 0, rollback 0, horizon 1 update). The geometry and
 * the native reads are on the game side (BtlHit_PortUdeRecipe). Everything that only existed for the network (input
 * delay, rollback, the z2 clock, the "physical input does not veto defence" policy) is gone: the UDE arbiter runs before
 * this and never lets it act on an update with physical input.
 *
 * Deliberate differences from Codex (each one has a test in test_ude_v5.c):
 *  - recipes 7, 8 (skills), 11, 12, 13 (stick-driven dashes) are not ported; where Codex would answer a close smash
 *    windup with recipe 13, a frontal guard is held instead of sending nothing;
 *  - the directional part of the stun guard / adaptive guard (up / down) is off unless guard_direction = 1;
 *  - a VanishStep after a held guard releases for 1 update, not 4 (UDE_VANISH_RELEASE below).
 * All times are game updates x 100 (rounded up by the game side): H = 100 is the Codex horizon.
 */
#include "ude_recipe.h"

#define H 100
/* Release updates before a VanishStep press when guard is still held from the previous packet. Codex uses 4 (a margin
   for the network: its packets travel 20 ms and are re-simulated); the game itself needs one released update, since
   BTLC_GUARD_H "pressed" is this update's command against the previous one (btl_input.c BtlInput_BuildCommands,
   condition 29..32). Off-line the plan is made at most 4 updates before the projectile (H + 300), so 4 releases would
   press on the update of the impact. The differential test builds with 4 to compare the transcription exactly. */
#ifndef UDE_VANISH_RELEASE
#define UDE_VANISH_RELEASE 1
#endif
#define X(i) (R[UDE_X_##i])

static const char *const kKind[UDE_K_COUNT] = {
    "none", "z_counter", "evade", "sonic_sway", "deflect", "charged_guard", "side_dash", "skill1", "skill2",
    "throw_break", "rush_evade", "retreat", "step_in", "backstep", "vanish_step"
};
const char *UdeRecipe_KindName(int k) { return (k >= 0 && k < UDE_K_COUNT) ? kKind[k] : "?"; }

void UdeRecipe_DefaultConfig(UdeRecipeConfig *c) {
    /* 1, 2, 3, 4, 5, 6, 9, 10, 14 (bit 13 is shared by 6 and 14 in Codex; here 14 has its own bit 13 and 6 bit 5) */
    c->kinds = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 8) | (1u << 9) | (1u << 13);
    c->adaptive_guard = 1;
    c->guard_direction = 0;
    c->ki_plan = 1;
    c->z_enabled = c->evade_enabled = c->sway_enabled = 1;
    c->plan_frames = 24;
}

void UdeRecipe_Reset(UdeRecipeState *s) {
    unsigned char *p = (unsigned char *)s;
    unsigned i;
    for (i = 0; i < sizeof(*s); i++) p[i] = 0;
    s->opp_anim = s->next_event = -1;    /* as Codex reset() / reset_online() */
    s->opp_frame100 = -100;
}

unsigned UdeRecipe_ToBattle(unsigned raw) {
    unsigned b = 0;
    if (raw & UDE_P_GUARD) b |= 0x00000001u | 0x00100000u;           /* BTLB_GUARD | BTLB_ANY_FACE */
    if (raw & UDE_P_DASH) b |= 0x00000002u | 0x00100000u;            /* BTLB_DASH */
    if (raw & UDE_P_BLAST) b |= 0x00000004u | 0x01000000u | 0x00100000u; /* BTLB_BLAST | BTLB_BLAST2 */
    if (raw & UDE_P_RUSH) b |= 0x00000008u | 0x00100000u;            /* BTLB_RUSH */
    if (raw & UDE_P_CHARGE) b |= 0x00000200u;                        /* BTLB_CHARGE */
    if (raw & UDE_P_UP) b |= 0x00000010u;                            /* BTLB_UP */
    if (raw & UDE_P_DOWN) b |= 0x00000020u;                          /* BTLB_DOWN */
    if (raw & UDE_P_LEFT) b |= 0x00000040u;                          /* BTLB_LEFT */
    if (raw & UDE_P_RIGHT) b |= 0x00000080u;                         /* BTLB_RIGHT */
    return b;
}

void UdeRecipe_Cancel(UdeRecipeState *s) {
    s->counter_hold = s->release = s->pulse_pending = s->pending_kind = 0;
    s->ki_stage = 0;
    s->scheduled = 0;
    s->plan_until = 0;
}

static int allowed(const UdeRecipeConfig *c, int kind) {
    if (kind < 1 || kind >= UDE_K_COUNT) return 0;
    if (kind == UDE_K_SKILL1 || kind == UDE_K_SKILL2 || kind == UDE_K_RETREAT || kind == UDE_K_STEPIN ||
        kind == UDE_K_BACKSTEP) return 0;                            /* not ported */
    return (c->kinds >> (kind - 1)) & 1u;
}

static void cancel_plan(UdeRecipeState *s) {
    s->counter_hold = s->release = s->pulse_pending = s->pending_kind = 0;
    s->ki_stage = 0;
}

static unsigned packet(int kind) {
    switch (kind) {
    case UDE_K_ZCOUNTER: return UDE_P_RUSH | UDE_P_UP;
    case UDE_K_SWAY: return UDE_P_GUARD | UDE_P_BLAST;
    case UDE_K_DEFLECT: return UDE_P_GUARD | UDE_P_RIGHT;
    case UDE_K_SIDESTEP: return UDE_P_DASH | UDE_P_RIGHT;
    case UDE_K_VANISH: return UDE_P_GUARD | UDE_P_RIGHT;
    default: return UDE_P_GUARD;
    }
}
static unsigned edge_of(int kind) {
    switch (kind) {
    case UDE_K_ZCOUNTER: return UDE_P_RUSH | UDE_P_UP;
    case UDE_K_SWAY: return UDE_P_BLAST;
    case UDE_K_DEFLECT: return UDE_P_RIGHT;
    case UDE_K_SIDESTEP: case UDE_K_RETREAT: case UDE_K_STEPIN: case UDE_K_BACKSTEP: return UDE_P_DASH;
    default: return UDE_P_GUARD;
    }
}

/* Codex fire_defense: a fresh edge now, or (when the edge is still held from the previous packet) one update of
   release first. Returns 0 when the recipe is off. */
static int fire(UdeRecipeState *s, const UdeRecipeConfig *c, int kind, unsigned *b, unsigned frame) {
    unsigned prior = s->out_valid ? s->prev_out : 0;
    if (!allowed(c, kind)) return 0;
    s->counter_kind = kind;
    s->last_pulse = frame;
    s->plan_until = frame + (unsigned)c->plan_frames;
    if (prior & edge_of(kind)) {
        s->pending_kind = kind;
        s->pulse_pending = kind == UDE_K_VANISH ? UDE_VANISH_RELEASE : 1;
        *b = 0;
        if (kind == UDE_K_SWAY || kind == UDE_K_DEFLECT) *b |= UDE_P_GUARD;
    } else {
        s->counter_hold = 1;
        *b = packet(kind);
    }
    if (kind >= 0 && kind < UDE_K_COUNT) s->fired[kind]++;
    return 2;
}

static int done(UdeRecipeState *s, UdeRecipeOut *o, int result, unsigned b, int kind, int unused) {
    unsigned prior = s->out_valid ? s->prev_out : 0;
    (void)unused;
    if (o) {
        o->raw = b;
        o->kind = kind;
        /* a recipe press starts on the update its edge bits appear (fire, or the end of a pulse release) */
        o->fired = kind > 0 && (b & edge_of(kind) & ~prior) != 0 && (kind != UDE_K_DEFLECT || (b & UDE_P_GUARD));
        o->result = result;
    }
    return result;
}

static int fired_packet(UdeRecipeState *s, const UdeRecipeConfig *c, UdeRecipeOut *o, int kind, unsigned frame) {
    unsigned b = 0;
    if (!fire(s, c, kind, &b, frame)) return done(s, o, UDE_RR_DECLINED, 0, kind, 0);
    return done(s, o, UDE_RR_PACKET, b, kind, 0);
}

void UdeRecipe_Commit(UdeRecipeState *s, unsigned raw) {
    s->prev_out = raw;
    s->out_valid = 1;
}

unsigned UdeRecipe_FromBattle(unsigned b) {
    unsigned r = 0;
    if (b & 0x1u) r |= UDE_P_GUARD;
    if (b & 0x2u) r |= UDE_P_DASH;
    if (b & 0x4u) r |= UDE_P_BLAST;
    if (b & 0x8u) r |= UDE_P_RUSH;
    if (b & 0x200u) r |= UDE_P_CHARGE;
    if (b & 0x10u) r |= UDE_P_UP;
    if (b & 0x20u) r |= UDE_P_DOWN;
    if (b & 0x40u) r |= UDE_P_LEFT;
    if (b & 0x80u) r |= UDE_P_RIGHT;
    return r;
}

int UdeRecipe_Step(UdeRecipeState *s, const UdeRecipeConfig *c, const int *R, unsigned frame, UdeRecipeOut *o) {
    unsigned b = 0;
    int kind = 0, melee, readyZ, readyD, back, act, throwing, rushing, grabbing, casting, captureCast, rushCast, slot;
    int until, incEta, inc, projectileFirst, stopped;

    if (!R || !X(OK)) {
        cancel_plan(s);
        s->scheduled = 0;
        return done(s, o, UDE_RR_NO_THREAT, 0, 0, 0);
    }
    stopped = X(STOPPED);
    /* own VanishStep / side step in progress (0x20..0x23, 0x35 with flags 0x44 and 0x46): leave it alone */
    if (X(VANISH_ACTIVE)) {
        cancel_plan(s);
        s->scheduled = 0;
        s->plan_until = 0;
        return done(s, o, UDE_RR_DECLINED, 0, 0, 0);
    }
    if (s->plan_until && (int)(frame - s->plan_until) >= 0) {
        cancel_plan(s);
        s->plan_until = 0;
        s->scheduled = 0;
    }
    if (!X(FREE)) {
        if (!(X(OPP_CAPTURE_SKILL) && X(D_LT_180))) {
            cancel_plan(s);
            s->scheduled = 0;
            return done(s, o, UDE_RR_DECLINED, 0, 0, 0);
        }
    }
    /* a new opponent attack (animation, event, action or member changed, or the frame went back): a new window */
    if (X(OPP_ANIM) != s->opp_anim || X(OPP_FRAME100) + 10 < s->opp_frame100 || X(FIRST_EVENT) != s->next_event ||
        X(OPP_ACT) != s->opp_act || X(OPP_MEMBER) != s->opp_member) {
        if (s->last_pulse && frame - s->last_pulse >= 2 && (s->counter_hold || s->pulse_pending || s->release)) {
            cancel_plan(s);
            s->plan_until = 0;
        }
        s->scheduled = 0;
        s->threat_since = 0;
        s->opp_anim = X(OPP_ANIM);
        s->next_event = X(FIRST_EVENT);
        s->opp_act = X(OPP_ACT);
        s->opp_member = X(OPP_MEMBER);
    }
    s->opp_frame100 = X(OPP_FRAME100);
    act = X(MY_ACT);
    back = X(BACK);

    /* stun guard: in a reaction, against a guardable strike in reach */
    if (c->adaptive_guard && X(REACTION) && X(ATK) >= 0 && X(D_LT_R)) {
        int dir = X(STUN_REACT);
        if (dir >= 0 && (dir == 0 || c->guard_direction)) {
            s->counter_hold = s->pulse_pending = s->release = 0;
            return done(s, o, UDE_RR_GUARD, UDE_P_GUARD | (unsigned)dir, 0, 0);
        }
    }
    /* the deflect stance (Action 0x3B) needs new GUARD + side commands to refill; only against a frontal ordinary
       projectile that is really there */
    if (allowed(c, UDE_K_DEFLECT) && (X(CAPS) & 0x20)) {
        if (act == 0x38 || act == 0x3B || act == 0x3D) {
            int v = X(VOLLEY);
            if (act == 0x3B && (((v & UDE_INC_HIT) && (v & UDE_INC_BEHIND)) || (!(v & UDE_INC_HIT) && back))) {
                cancel_plan(s);
                s->scheduled = 0;
                s->plan_until = 0;
                return done(s, o, UDE_RR_RELEASE, 0, 0, 0);
            }
            if ((v & UDE_INC_HIT) && !(v & UDE_INC_TECH) && !(v & UDE_INC_BEHIND) && (v & UDE_INC_REDIRECT) &&
                !(v & UDE_INC_CHARGED) && (!(X(PFLAGS) & 2) || (v & UDE_INC_ABSORB))) {
                s->counter_hold = s->pulse_pending = s->release = 0;
                s->counter_kind = UDE_K_DEFLECT;
                b = UDE_P_GUARD;
                if (act != 0x3D) b |= (frame & 2) ? UDE_P_RIGHT : UDE_P_LEFT;
                s->ki_stage = 0;
                s->plan_until = frame + (unsigned)c->plan_frames;
                return done(s, o, UDE_RR_PACKET, b, UDE_K_DEFLECT, 0);
            }
        }
    }
    /* an ordinary ki cast (action 0xAE, animations 0x71 / 0x72) is a committed threat before its record exists:
       stage the guard once per cast, with an absolute deadline of 12 updates */
    if (c->ki_plan && allowed(c, UDE_K_DEFLECT)) {
        int cast = X(KI_CAST);
        if (X(OPP_ACT) != 0xAE) {
            s->ki_seen = 0;
        } else if (cast && !s->ki_seen) {
            s->ki_seen = 1;
            s->ki_until = frame + 12;
            if (!s->counter_hold && !s->pulse_pending && !s->release && !stopped && !X(REACTION) && !X(MY_TECH) &&
                (X(OWN_FLAGS) & 0x31) && (X(CAPS) & 0x20) && !back) s->ki_stage = 1;
        }
        if (s->ki_stage) {
            int p = X(PENDING24);
            int frontal = (p & UDE_INC_HIT) && !(p & UDE_INC_TECH) && !(p & UDE_INC_CHARGED) && (p & UDE_INC_REDIRECT) &&
                          !(p & UDE_INC_BEHIND);
            int continuation = s->ki_seen && X(OPP_ACT) == 0xAE && X(KI_CONT);
            int threat = cast || continuation || frontal;
            if (!threat || ((p & UDE_INC_HIT) && (p & UDE_INC_BEHIND)) || X(URGENT) || stopped || X(REACTION) ||
                (int)(frame - s->ki_until) >= 0) {
                s->ki_stage = 0;
                s->plan_until = 0;
            } else {
                if (act == 0x38 && (cast || continuation || frontal)) {
                    s->ki_stage = 0;
                    s->scheduled = 1;
                    return fired_packet(s, c, o, UDE_K_DEFLECT, frame);
                }
                return done(s, o, UDE_RR_PACKET, UDE_P_GUARD, UDE_K_DEFLECT, 0);
            }
        }
    }
    if (s->skill_wait > 0 && !stopped) s->skill_wait--;
    if (X(GLOBAL_STOPPED) && s->pulse_pending) {
        s->counter_kind = s->pending_kind;
        s->counter_hold = 2;
        s->pulse_pending = 0;
    }
    if ((s->counter_hold || s->pulse_pending || s->release) && !allowed(c, s->pulse_pending ? s->pending_kind : s->counter_kind)) {
        cancel_plan(s);
        s->plan_until = 0;
        s->scheduled = 0;
    }
    if (s->counter_hold > 0) {
        b = packet(s->counter_kind);
        if (!stopped && !--s->counter_hold) s->release = 2;
        return done(s, o, UDE_RR_PACKET, b, s->counter_kind, 0);
    }
    if (s->pulse_pending > 0) {
        int fired = 0;
        if (s->pending_kind == UDE_K_SWAY || s->pending_kind == UDE_K_DEFLECT) b |= UDE_P_GUARD;
        if (!stopped && !--s->pulse_pending) {
            s->counter_kind = s->pending_kind;
            s->counter_hold = 2;
        }
        return done(s, o, UDE_RR_PACKET, b, s->pending_kind, fired);
    }
    if (s->release > 0) {
        if (s->counter_kind == UDE_K_DEFLECT) {
            int p = X(PENDING24);
            if ((p & UDE_INC_HIT) && !(p & UDE_INC_BEHIND)) b |= UDE_P_GUARD;
        }
        if (!--s->release) s->plan_until = 0;
        return done(s, o, b ? UDE_RR_PACKET : UDE_RR_RELEASE, b, s->counter_kind, 0);
    }

    inc = X(INC);
    incEta = X(INC_ETA100);
    if ((inc & UDE_INC_HIT) && X(PROTECTED)) {   /* the game already accepts an active protection: do not spend an edge */
        cancel_plan(s);
        s->scheduled = 0;
        s->plan_until = 0;
        return done(s, o, UDE_RR_DECLINED, 0, 0, 0);
    }
    throwing = X(THROWING);
    rushing = X(RUSHING);
    grabbing = X(GRABBING);
    casting = X(CASTING);
    captureCast = X(CAPTURE);
    rushCast = X(RUSHCAST);
    slot = X(SLOT);
    melee = X(MELEE);
    until = X(UNTIL100);

    /* smash preparation without an attack id, in close range */
    if (X(ATK) < 0 && X(CHARGE_WINDUP) && !(inc & UDE_INC_HIT) && !X(REACTION) && !X(MY_TECH) && X(CHARGE_UNSAFE)) {
        s->scheduled = 0;
        if ((X(OWN_FLAGS) & 0x31) && allowed(c, UDE_K_BACKSTEP) && (!s->last_pulse || frame - s->last_pulse >= 10))
            return fired_packet(s, c, o, UDE_K_BACKSTEP, frame);
        return done(s, o, UDE_RR_GUARD, back ? 0 : UDE_P_GUARD, 0, 0);   /* (Codex: recipe 13; not ported) */
    }
    if (!melee && !(inc & UDE_INC_HIT) && !casting && !(throwing && X(D_LT_R4)) && !(rushing && X(UNTIL_FLOOR100) < H + 800) &&
        !(grabbing && X(D_LT_100))) {
        s->threat_since = 0;
        return done(s, o, UDE_RR_NO_THREAT, 0, 0, 0);
    }
    if (!s->threat_since) s->threat_since = frame;
    readyZ = allowed(c, UDE_K_ZCOUNTER) && c->z_enabled && (X(OWN_FLAGS) & 0x31) && X(W1070) <= -30;
    readyD = allowed(c, UDE_K_EVADE) && c->evade_enabled && X(FLAG5) && X(DODGE_WIN) && X(W1068) <= -30 && !X(NO_EVADE);
    if (!X(FIRST_HIT)) readyZ = readyD = 0;      /* later volumes cannot be countered nor evaded */
    /* re-arm only once the native cooldown allows another first-hit window */
    if (s->scheduled && ((s->counter_kind == UDE_K_ZCOUNTER && readyZ) || (s->counter_kind == UDE_K_EVADE && readyD)) &&
        frame - s->last_pulse > 8 && until > 0) s->scheduled = 0;
    /* keep the native redirect / absorb / sway stances through a burst */
    if ((act == 0x3B || act == 0x3D) && (inc & UDE_INC_HIT) && !(inc & UDE_INC_BEHIND)) {
        b = UDE_P_GUARD;
        if (act == 0x3B) b |= (frame & 2) ? UDE_P_RIGHT : UDE_P_LEFT;
        return done(s, o, UDE_RR_PACKET, b, UDE_K_DEFLECT, 0);
    }
    if (act == 0x67 && melee && X(ATK) >= 0 && X(ATK) < 11 && !back) return done(s, o, UDE_RR_PACKET, UDE_P_GUARD, UDE_K_SWAY, 0);
    projectileFirst = X(PROJ_FIRST);
    /* a technique not yet released: the native 0x400 release event plus the shot's travel */
    if (!(inc & UDE_INC_HIT) && casting && !rushCast && !captureCast && slot >= 2 && slot <= 4 && !s->scheduled && !stopped &&
        allowed(c, UDE_K_EVADE) && X(FLAG5) && !X(NO_EVADE) && (X(OWN_FLAGS) & 0xB1) && X(W106C) <= -30 && X(CAST_ETA100) >= 0 &&
        X(CAST_ETA100) <= H) kind = UDE_K_EVADE;
    if (projectileFirst) {
        int pflags = X(PFLAGS), caps = X(CAPS);
        if (!(inc & UDE_INC_BEHIND) && ((inc & UDE_INC_REDIRECT) || ((inc & UDE_INC_ABSORB) && (pflags & 2))) && (caps & 0x20)) {
            if ((pflags & 2) && !(inc & UDE_INC_ABSORB)) {
                if (incEta <= H + 300) kind = UDE_K_SIDESTEP;
            } else {
                if ((inc & UDE_INC_CHARGED) && !X(REACTION) && !(pflags & 2) && X(W1068) <= -30) {
                    if (incEta <= H + 100) kind = UDE_K_CHARGED;
                } else if ((act == 0x38 || act == 0x3B || act == 0x3D || X(W1074) <= -15) && incEta <= H + 700) {
                    kind = UDE_K_DEFLECT;     /* the native stance does not need the perfect-deflect cooldown */
                }
            }
        }
        if ((inc & UDE_INC_TECH) && (inc & UDE_INC_EVADABLE) && !kind && X(FLAG5) && !X(NO_EVADE) && (X(OWN_FLAGS) & 0xB1) &&
            X(W106C) <= -30 && incEta <= H + 50) kind = UDE_K_EVADE;
        if (!kind && ((inc & UDE_INC_BEHIND) || !(inc & UDE_INC_REDIRECT)) && incEta <= H + 300 && !X(REACTION) &&
            (!s->last_pulse || frame - s->last_pulse > 10))
            kind = (((inc & UDE_INC_BEHIND) || !(inc & UDE_INC_TECH)) && X(VANISH)) ? UDE_K_VANISH : UDE_K_SIDESTEP;
    }
    if (kind && !allowed(c, kind)) kind = 0;
    if (!projectileFirst && (throwing || rushing) && (!throwing || X(D_LT_R2)) && until <= (rushing ? H : H + 100) && !s->scheduled) {
        int timer = rushing ? X(W107C) : X(W1078);
        int eligible = (rushing && !X(OPP_A5)) ? ((X(OWN_FLAGS) & 0xB1) && !X(NO_EVADE)) : (!back && (X(OWN_FLAGS) & 0x31));
        if (eligible && X(FLAG5) && !(throwing && (X(OWN_FLAGS) & 0x800)) && timer <= -30) kind = rushing ? UDE_K_RUSHEVADE : UDE_K_THROWBREAK;
        else if (!rushing && readyZ && until > 0 && until <= H) kind = UDE_K_ZCOUNTER;
        else if (!X(REACTION) && until <= H)
            kind = (throwing && back && X(VANISH)) ? UDE_K_VANISH : (rushing && (X(CAPS) & 1) && X(FLAG5)) ? UDE_K_STEPIN : UDE_K_SIDESTEP;
    }
    if (!projectileFirst && !kind && rushing && s->scheduled && s->counter_kind == UDE_K_RUSHEVADE && X(W107C) <= 0 &&
        frame - s->last_pulse >= 6 && until <= H + 50 && (X(CAPS) & 1) && X(FLAG5) && !X(REACTION)) kind = UDE_K_STEPIN;
    if (!projectileFirst && ((grabbing && X(D_LT_100)) || (captureCast && X(D_LT_140))) && !X(REACTION) && !kind)
        kind = UDE_K_RETREAT;              /* (Codex: protective skill or retreat; neither is ported) */
    if (!projectileFirst && melee && !throwing && !rushing && !s->scheduled && until > 0) {
        /* the native Sway covers a frontal rush across its stance window; a one-update Z is more fragile */
        if (allowed(c, UDE_K_SWAY) && c->sway_enabled && X(ATK) >= 0 && X(ATK) < 11 && !back && (X(CAPS) & 0x80) && X(OPP_UNK2) != 4 &&
            X(MY_KI) > 1000 && (act == 0xB || act == 0xE || act == 0xF || act == 0x38) && until <= H + 300) {
            kind = UDE_K_SWAY;
        } else {
            unsigned prior = s->out_valid ? s->prev_out : 0;
            int need = (prior & (UDE_P_RUSH | UDE_P_UP)) ? 2 : 1;   /* z2_phase at delay 0: n == 1, or 2 after a release */
            if (readyZ && !stopped && X(OPP_FREEZE) <= 0 && until % 100 == 0 && until / 100 == need) kind = UDE_K_ZCOUNTER;
            else if (until <= H && readyD && (X(ATK_FLAGS) & 1)) kind = UDE_K_EVADE;
        }
    }
    if (kind && !allowed(c, kind)) kind = 0;
    if (kind) {
        s->scheduled = 1;
        return fired_packet(s, c, o, kind, frame);
    }
    if (X(REACTION)) {
        int dir = (melee && c->adaptive_guard) ? X(STUN_DIR) : -1;
        if (dir >= 0 && (dir == 0 || c->guard_direction)) return done(s, o, UDE_RR_GUARD, UDE_P_GUARD | (unsigned)dir, 0, 0);
        return done(s, o, UDE_RR_DECLINED, 0, 0, 0);    /* leave the recovery input to the player */
    }
    /* a press during a distant cast burns the short evasion window: keep the fresh edge */
    if ((casting && !(inc & UDE_INC_HIT)) || (rushing && until > H) || (throwing && until > H + 100) ||
        ((inc & UDE_INC_HIT) && ((inc & UDE_INC_TECH) || (inc & UDE_INC_CHARGED)) && incEta > H + 100)) {
        int waitThrow = throwing && !back && (X(OWN_FLAGS) & 0x31) && !(X(OWN_FLAGS) & 0x800) && X(FREE) && X(FLAG5) && X(W1078) <= -30;
        if (!waitThrow && !rushing && !captureCast && (back || X(D_LT_R2)) && (!s->last_pulse || frame - s->last_pulse > 12)) {
            int k = (throwing && back && X(VANISH)) ? UDE_K_VANISH : UDE_K_SIDESTEP;
            if (allowed(c, k)) return fired_packet(s, c, o, k, frame);
            return done(s, o, UDE_RR_DECLINED, 0, k, 0);
        }
        return done(s, o, UDE_RR_GUARD, 0, 0, 0);
    }
    /* already guarding with the opponent behind: release once, so the next guard turns first */
    if ((back || ((inc & UDE_INC_HIT) && (inc & UDE_INC_BEHIND))) && act == 0x38 && (!s->last_pulse || frame - s->last_pulse > 12)) {
        s->last_pulse = frame;
        s->release = 1;
        s->counter_kind = 0;
        return done(s, o, UDE_RR_PACKET, 0, 0, 0);
    }
    /* keep a fresh edge until the impact reaches the lead (vanishing pursuit set-ups have no impact event) */
    if (melee && !projectileFirst && (X(ATK_FLAGS) & 1) && (readyD || (X(OWN_FLAGS) & 0x200000) || (X(FIRST_EVENT) < 0 && X(SEQ_DONE))) &&
        until > H) return done(s, o, UDE_RR_GUARD, 0, 0, 0);
    if (!c->adaptive_guard) return done(s, o, UDE_RR_DECLINED, 0, 0, 0);
    if (melee && !back) b |= UDE_P_GUARD | (c->guard_direction ? (unsigned)X(GUARD_DIR) : 0u);
    else if ((inc & UDE_INC_HIT) || rushing || back) b |= UDE_P_GUARD;
    /* prepare a fresh guard edge before a legal evasion / throw window */
    if (until > H && X(UNTIL_FLOOR100) < H + 300 && (throwing || rushing || (!readyZ && readyD))) b &= ~UDE_P_GUARD;
    return done(s, o, UDE_RR_GUARD, b, 0, 0);
}
