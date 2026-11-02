/*
 * BT3 UDE Hybrid v5: the input recipes of the Codex R8 defence (OnlineDefense.c, online_defense), ported to the
 * off-line engine. Pure scalar C, no game header, no allocation, no I/O: it reads one recipe snapshot per frame (filled
 * on the game side by BtlHit_PortUdeRecipe, src/battle/btl_char_hit.c) and proposes a pad packet made only of legal
 * inputs (guard, rush + up, guard + blast, guard + side, dash + side). Nothing in the game's memory is ever written.
 *
 * Off-line semantics: the hook runs once per game update, before that update's action dispatch and hit tests
 * (src/battle/btl_char_mgr.c steps 2 -> 4), so the Codex pad delay is 0 and its horizon is 1 update. The online
 * simulation clock of Codex (z2_phase) reduces to "the first impact is in this update" (n == 1), or n == 2 when the
 * edge must first be released.
 *
 * The packet is built in "raw" bits (the Codex pad layout: directions 0x10 up, 0x20 right, 0x40 down, 0x80 left, and the
 * key-config masks below for the actions) and converted to the battle word by UdeRecipe_ToBattle.
 */
#ifndef UDE_RECIPE_H
#define UDE_RECIPE_H

#ifdef __cplusplus
extern "C" {
#endif

/* snapshot fields: the order of BtlHit_PortUdeRecipe's R[] (tests/check_recipe_ids.py compares both lists) */
enum {
    UDE_X_OK = 0, UDE_X_OPP_ANIM, UDE_X_OPP_FRAME100, UDE_X_FIRST_EVENT, UDE_X_OPP_ACT, UDE_X_OPP_MEMBER,
    UDE_X_MY_ACT, UDE_X_OWN_FLAGS, UDE_X_CAPS, UDE_X_PFLAGS, UDE_X_FREE, UDE_X_STOPPED, UDE_X_GLOBAL_STOPPED,
    UDE_X_VANISH_ACTIVE, UDE_X_OPP_CAPTURE_SKILL, UDE_X_D_LT_180, UDE_X_STUN_REACT, UDE_X_VOLLEY, UDE_X_BACK,
    UDE_X_KI_CAST, UDE_X_KI_CONT, UDE_X_PENDING24, UDE_X_URGENT, UDE_X_INC, UDE_X_INC_ETA100, UDE_X_PROTECTED,
    UDE_X_ATK, UDE_X_ATK_FLAGS, UDE_X_DIST100, UDE_X_RANGE100, UDE_X_THROWING, UDE_X_CASTING, UDE_X_SLOT,
    UDE_X_RUSHCAST, UDE_X_RUSHTYPE, UDE_X_CAPTURE, UDE_X_RUSHING, UDE_X_GRABBING, UDE_X_MELEE, UDE_X_UNTIL100,
    UDE_X_SEQ_DONE, UDE_X_CHARGE_WINDUP, UDE_X_CHARGE_UNSAFE, UDE_X_W1068, UDE_X_W106C, UDE_X_W1070, UDE_X_W1074,
    UDE_X_W1078, UDE_X_W107C, UDE_X_FLAG5, UDE_X_NO_EVADE, UDE_X_DODGE_WIN, UDE_X_FIRST_HIT, UDE_X_OPP_FREEZE,
    UDE_X_CAST_ETA100, UDE_X_VANISH, UDE_X_OPP_A5, UDE_X_OPP_UNK2, UDE_X_MY_KI, UDE_X_REACTION, UDE_X_OPP_REACTION,
    UDE_X_GUARD_DIR, UDE_X_STUN_DIR, UDE_X_MY_TECH, UDE_X_PROJ_FIRST, UDE_X_D_LT_R2, UDE_X_D_LT_R4, UDE_X_D_LT_100,
    UDE_X_D_LT_140, UDE_X_D_LT_R, UDE_X_UNTIL_FLOOR100,
    UDE_X_COUNT
};
#define UDE_X_NONE100 100000   /* "no contact" (1000 updates) */

/* incoming-hit bits (UDE_X_INC, UDE_X_VOLLEY, UDE_X_PENDING24) */
#define UDE_INC_HIT 1
#define UDE_INC_TECH 2
#define UDE_INC_CHARGED 4
#define UDE_INC_BEHIND 8
#define UDE_INC_REDIRECT 16
#define UDE_INC_ABSORB 32
#define UDE_INC_EVADABLE 64
#define UDE_INC_SKILL 128

/* raw packet bits (Codex layout; the action masks are those of its test harness, BtlInput_GetKeyMask order) */
#define UDE_P_UP 0x10u
#define UDE_P_RIGHT 0x20u
#define UDE_P_DOWN 0x40u
#define UDE_P_LEFT 0x80u
#define UDE_P_RUSH 0x8000u     /* action 0 */
#define UDE_P_DASH 0x4000u     /* action 1 */
#define UDE_P_GUARD 0x2000u    /* action 2 */
#define UDE_P_BLAST 0x1000u    /* action 3 */
#define UDE_P_CHARGE 0x100u    /* action 5 */

/* the recipes (Codex counter kinds) */
enum UdeRecipeKind {
    UDE_K_NONE = 0,
    UDE_K_ZCOUNTER = 1,    /* UP + RUSH, both fresh: condition 38 (0x26), window +0x1070, first hit only */
    UDE_K_EVADE = 2,       /* fresh GUARD: condition 39, window +0x1068 (melee) / +0x106C (technique) */
    UDE_K_SWAY = 3,        /* GUARD held + fresh BLAST2: condition 95 (Sonic Sway, flag 0x4E path) */
    UDE_K_DEFLECT = 4,     /* GUARD + fresh side: condition 43 (0x2B), Action 0x3B stance / +0x1074 */
    UDE_K_CHARGED = 5,     /* fresh GUARD against a charged ki blast */
    UDE_K_SIDESTEP = 6,    /* DASH + side: lateral dash out of a projectile's path */
    UDE_K_SKILL1 = 7,      /* CHARGE + GUARD (skill 1): off by default, spends the blast gauge */
    UDE_K_SKILL2 = 8,      /* CHARGE + GUARD + UP (skill 2): off by default */
    UDE_K_THROWBREAK = 9,  /* fresh GUARD: window +0x1078 (BtlHit_CheckThrowBreak), from the front */
    UDE_K_RUSHEVADE = 10,  /* fresh GUARD: window +0x107C (BtlHit_CheckRush) */
    UDE_K_RETREAT = 11,    /* DASH + stick away: not ported (needs the stick byte) */
    UDE_K_STEPIN = 12,     /* DASH: not ported */
    UDE_K_BACKSTEP = 13,   /* DASH + stick away: not ported */
    UDE_K_VANISH = 14,     /* GUARD + fresh side from the rush handler 0x44: VanishStep / Super Counter (ki AmountA) */
    UDE_K_COUNT = 15
};

/* what UdeRecipe_Step returns */
enum UdeRecipeResult {
    UDE_RR_NO_THREAT = 0,  /* nothing the recipes recognise: the v4 policy may decide */
    UDE_RR_GUARD = 1,      /* handled: the packet holds guard (or deliberately keeps a fresh edge: empty packet) */
    UDE_RR_PACKET = 2,     /* a recipe packet (a press, its release, or a stance renewal) */
    UDE_RR_RELEASE = 3,    /* handled: release everything (stance from behind, end of a plan) */
    UDE_RR_DECLINED = 4    /* recognised, but the recipe for it is switched off: nothing is sent */
};

typedef struct UdeRecipeConfig {
    unsigned kinds;        /* bit (k - 1) enables recipe k (14 uses bit 13) */
    int adaptive_guard;    /* Codex DefenseOptions[2]: plain guard between recipes (default 1) */
    int guard_direction;   /* add up / down to a frontal guard from the attack's guard kinds (default 0: experimental) */
    int ki_plan;           /* stage the deflect stance on an observed ki cast (Codex policy 3; default 1) */
    int z_enabled, evade_enabled, sway_enabled;   /* Codex OnlineOptions[0] bits 1 / 2 / 4 (default 1) */
    int plan_frames;       /* Codex OnlineOptions[2]: a plan expires after this many updates (default 24) */
} UdeRecipeConfig;

typedef struct UdeRecipeState {
    int counter_hold, pulse_pending, release, pending_kind, counter_kind, scheduled, skill_wait;
    unsigned last_pulse, plan_until, threat_since;
    int opp_anim, opp_frame100, next_event, opp_act, opp_member;
    int ki_stage, ki_seen;
    unsigned ki_until;
    unsigned prev_out;     /* the packet sent on the previous update (raw bits) */
    int out_valid;
    unsigned fired[UDE_K_COUNT];
} UdeRecipeState;

typedef struct UdeRecipeOut {
    unsigned raw;          /* raw packet bits */
    int kind;              /* recipe of the packet (0: plain guard / none) */
    int fired;             /* 1 on the update a recipe press starts (its fresh edge) */
    int result;            /* UdeRecipeResult */
} UdeRecipeOut;

void UdeRecipe_DefaultConfig(UdeRecipeConfig *c);
void UdeRecipe_Reset(UdeRecipeState *s);
/* Drop the current plan (an update on which the engine may not act: the player's input, a pending request...). */
void UdeRecipe_Cancel(UdeRecipeState *s);
/* One update. `R` is the snapshot (UDE_X_COUNT ints), `frame` the update counter. */
int UdeRecipe_Step(UdeRecipeState *s, const UdeRecipeConfig *c, const int *R, unsigned frame, UdeRecipeOut *out);
/* raw packet -> battle word (BTLB_* of include/battle/btl_input.h): GUARD 0x1 (+ANY_FACE), DASH 0x2, BLAST 0x4
   (+BLAST2), RUSH 0x8, UP 0x10, DOWN 0x20, LEFT 0x40, RIGHT 0x80, CHARGE 0x200. */
unsigned UdeRecipe_ToBattle(unsigned raw);
const char *UdeRecipe_KindName(int k);
/* what was really sent on this update (the battle word as the game records it, converted back): the next update's
   fresh-edge tests compare with it, as the game's BtlInput_BuildCommands compares with the previous record */
void UdeRecipe_Commit(UdeRecipeState *s, unsigned raw);
unsigned UdeRecipe_FromBattle(unsigned battle);

#ifdef __cplusplus
}
#endif
#endif
