/*
 * BT3 Universal Defense Engine (UDE): automatic guard for the Tenkaichi3Decomp PC port, offline play only.
 *
 * The engine core (ude.c) is pure scalar C with no game headers: it reads one UdeSnapshot per frame and returns the
 * battle button word to use. The game adapter (ude_game.c) fills the snapshot from the game's own accessors
 * (BtlCharApi_*, the same ones the CPU opponent's AI reads) and is called from BtlInput_BuildRecord, so the defence
 * goes through the exact input path a human pad uses: key config is already applied, and nothing in the game's
 * memory is written except the button word of the player's own fighter.
 *
 * Pipeline per frame: Safety -> Observer (snapshot) -> Threat classifier -> Defense policy -> Input arbiter ->
 * Timing controller. Only BTLB_GUARD can ever be added; physical bits are never cleared.
 */
#ifndef UDE_H
#define UDE_H
#include "ude_recipe.h"   /* Hybrid v5: the Codex R8 recipes (pure scalar too) */

#ifdef __cplusplus
extern "C" {
#endif

/* Battle button bits (include/battle/btl_input.h). Copied, not included, so the core builds anywhere. */
#define UDE_B_GUARD    0x00000001u /* action 2, default Circle */
#define UDE_B_DIR_MASK 0x000000F0u /* up / down / left / right (d-pad or left stick) */
#define UDE_B_ANY_FACE 0x00100000u /* any of actions 0..3; the game derives it from the face buttons */
/* Bits that are NOT a deliberate action of the player (so they never block an intervention by themselves):
   directions (handled separately), the derived any-face copy, the not-remapped physical face copies (27..30) only
   count through the remapped bits. */
#define UDE_B_DERIVED  (UDE_B_ANY_FACE | 0x78000000u | 0x01000000u)
#define UDE_ACT_BLOW_AWAY 0xD5    /* BtlAct_BlowAwayHandler (gBtlActHandlers) */

enum UdeThreat {           /* also the values of kUdeThreatByAction (generated) */
    UDE_T_NONE = 0,
    UDE_T_MELEE = 1,       /* table-driven attack actions 0x70..0xAD */
    UDE_T_RUSH = 2,        /* rush sequences 0x12D..0x12F, 0x139..0x13B */
    UDE_T_TECHNIQUE = 3,   /* Blast 2 / Ultimate actions 0x105.. */
    UDE_T_SKILL = 4,       /* Blast 1 skill actions: never defended (mostly self effects) */
    UDE_T_CLASH = 5,       /* never touched: won by mashing */
    UDE_T_BLAST = 6,       /* a projectile flying at the player (BtlCharApi_FindIncomingBlast) */
    UDE_T_THROW = 7        /* the opponent's attack id is 0x55 (BtlHit_CheckThrowBreak): guard does not stop it */
};
#define UDE_T_COUNT 8

/* How sure the classifier is (Etapa 3: confirmed / probable / unknown). */
enum UdeConfidence { UDE_C_NONE = 0, UDE_C_UNKNOWN = 1, UDE_C_PROBABLE = 2, UDE_C_CONFIRMED = 3 };

/* What the engine sends. HOLD: guard held (v1). TAP: one fresh guard press, then release: opens the defence windows of
   BtlMove_UpdateDefenseTimers (evasion, throw break) without holding guard where guard cannot work. PRESS_HOLD: the
   same press, timed, then guard held. */
enum UdeResponse { UDE_RESP_NONE = 0, UDE_RESP_HOLD = 1, UDE_RESP_TAP = 2, UDE_RESP_PRESS_HOLD = 3,
                   UDE_RESP_RECOVER = 4, /* face presses on alternate frames while knocked away (action 0xD5): the game's
                                            BtlAct_BlowAwayHandler counts them and switches to 0xD7 (early recovery) */
                   UDE_RESP_RECIPE = 100 /* Hybrid v5: 100 + UdeRecipeKind (a Codex R8 recipe packet; 100 = recipe guard) */ };

/* The defence windows at fighter +0x1068.. (src/battle/btl_char_move.c BtlMove_UpdateDefenseTimers). Each counts down
   to a floor; a fresh guard press (input condition 0x27, with fighter flag 5) re-opens it ONLY at the floor; a press
   while it is above the floor sets it to -1 (no window, cooldown restarts). */
enum { UDE_W_EVADE = 0,  /* +0x1068: avoids the first hit of a flag-1 attack (BtlHit_CheckDodge), 2..4 frames */
       UDE_W_BLAST = 1,  /* +0x106C: blast side step (btl_char_coll_b.c), 2 frames (4 with flag 0x138) */
       UDE_W_COUNTER = 2,/* +0x1070: opened by UP + RUSH (condition 0x26), not by guard: never used by the engine */
       UDE_W_PERFECT = 3,/* +0x1074: GUARD held + side press (condition 0x2B), character bit: never used */
       UDE_W_THROW = 4,  /* +0x1078: throw break / repel (BtlHit_CheckThrowBreak), 5 frames */
       UDE_W_RUSH = 5,   /* +0x107C: rush technique evasion (BtlHit_CheckRush), 2..4 frames */
       UDE_W_COUNT = 6 };
#define UDE_W_FLOOR (-30)

enum UdeMode { UDE_OFF = 0, UDE_ON = 1, UDE_SUSPENDED = 2 };

enum UdeReason {           /* why the engine did not (or could not) intervene this frame */
    UDE_R_NONE = 0, UDE_R_DISABLED, UDE_R_ONLINE, UDE_R_REPLAY, UDE_R_CPU_SIDE, UDE_R_NOT_FIGHTING, UDE_R_TIME_STOP,
    UDE_R_BAD_SNAPSHOT, UDE_R_NO_THREAT, UDE_R_MANUAL, UDE_R_MOVING, UDE_R_BUSY, UDE_R_OUT_OF_REACH,
    UDE_R_BEHIND, UDE_R_NO_KI, UDE_R_HOLD_CAP, UDE_R_NOT_ACCEPTED, UDE_R_COOLDOWN,
    UDE_R_REQUEST_PENDING,  /* the player's own action request is waiting for the next dispatch: never override it */
    UDE_R_WAIT_TIMING,      /* a timed press is planned, contact not close enough yet */
    UDE_R_WINDOW_COOLING,   /* a press now would reset the game's window (anti-mash): wait */
    UDE_R_NOT_EVADABLE,     /* behind / throw from behind / attack without the evadable flag: no valid defence */
    UDE_R_MANUAL_ACTION,    /* v5: an action the player started is still running (Codex R8 ManualClaim) */
    UDE_R_RECIPE_DECLINED,  /* v5: the recipe engine recognised the situation, but its answer is off or not legal now */
    UDE_R_RECIPE_EDGE,      /* v5: the recipe engine keeps the guard edge fresh for the coming window (nothing sent) */
    UDE_R_COUNT
};

/* Everything the engine reads, filled once per frame by the adapter. Scalars only. */
typedef struct UdeSnapshot {
    int valid;                 /* 0: the adapter could not read the fighters: suspend */
    int online;                /* Port_NetActive(): never act online */
    int replay;                /* BattleReplay_IsActive(): the replay overwrites input anyway */
    int cpu_side;              /* own fighter's input is injected by the AI */
    int fighting;              /* own fighter takes input this frame (flags 2/3, not 0x136) */
    int time_stopped;          /* BtlChars_IsTimeStopped() */
    int my_action, my_action_frame;
    int opp_action, opp_action_frame;
    int opp_attacking;         /* BtlCharApi_CheckUnkCAC(opp): the AI's own "opponent attacking" test */
    int opp_skill_kind;        /* BtlCharApi_GetOppSkillKind(me): -1 none; 1..2 are what the AI guards against */
    int incoming_blast;        /* BtlCharApi_FindIncomingBlast(me, 0) != -1 */
    int close;                 /* BtlCharApi_IsClose(me): fighter flag 0x13, opponent within reach */
    int behind;                /* opponent behind the player (guard fails from behind); -1 unknown */
    int my_ki;                 /* 0..100000 */
    int my_hp;
    /* Extended reads (BtlHit_PortUdeQuery / BtlCharApi_PortBlastEta10, game-side helpers). Ignored unless have_ext. */
    int have_ext;
    int anim_flags;            /* own animation flags: 0xB1 evasion possible, 0x31 throw break / counter possible */
    int opp_attack_id;         /* -1 none, 0x55 throw */
    int opp_attack_flags;      /* bit 1: evadable first hit; 0x100000: evasion turns into a counter (20000 ki) */
    int opp_hit_active;        /* the opponent's hit volumes are active this frame: contact is being tested NOW */
    int flag5;                 /* own fighter flag 5: windows can only open with it */
    int no_evade;              /* own fighter flag 0x5A */
    int my_request_pending;    /* own action request waiting (BtlAct_GetRequested != -1) */
    int blast_eta10;           /* frames x 10 until the nearest incoming blast arrives, -1 none */
    int win[UDE_W_COUNT];      /* the defence windows */
    int recover_ok;            /* query 23: the character can recover from a knock-away by pressing (flags2 0x40, no 0x1D) */
    int game_free;             /* query 24: BtlChar_IsFree */
    /* Hybrid v5: the recipe snapshot (BtlHit_PortUdeRecipe, src/battle/btl_char_hit.c). Ignored unless have_recipe. */
    int have_recipe;
    int recipe[UDE_X_COUNT];
} UdeSnapshot;

typedef struct UdeConfig {
    int enabled;               /* master switch (F7 toggles at run time) */
    int guard_melee, guard_rush, guard_technique, guard_blast;
    int manual_grace;          /* frames after any manual action button during which the engine stays out (default 4) */
    int allow_while_moving;    /* 0: never guard while a direction is held (movement is never blocked) */
    int hold_tail;             /* frames guard stays held after the threat ends (default 3) */
    int hold_cap;              /* longest continuous injected guard (default 90 = 3 s), then cooldown */
    int cooldown;              /* frames without injection after the cap (default 20) */
    int accept_frames;         /* frames after which the game must show a guard action, else NOT_ACCEPTED (default 8) */
    int suspend_frames;        /* suspension after repeated NOT_ACCEPTED (default 60) */
    int min_ki;                /* do not guard below this ki (guard can spend ki), default 0 */
    int skip_behind;           /* 1: no guard when the snapshot says the threat is behind (default 1) */
    int tap_defense;           /* 1: timed presses where guard cannot work (behind, throws, blasts from behind) (default 1) */
    int front_timing;          /* 1: also time the press of front guards to the contact (dodge chance); 0: hold at once (v1) */
    int tap_lead;              /* press when contact is at most this many frames away (default 1) */
    int request_guard;         /* 1: never act while the player's own action request is pending (default 1) */
    int recover;               /* 1: recovery presses while knocked away (cycle 5; default 0 until an in-game A/B) */
    /* Hybrid v5 (all 0 here, so v4 behaviour is the default of the core; the game adapter turns them on) */
    int recipes;               /* 1: the Codex R8 recipes decide first; the v4 policy only where they see no threat */
    int manual_claim;          /* 1: stay out while an action the player started is still running (Codex R8) */
    UdeRecipeConfig rcfg;
} UdeConfig;

typedef struct UdeStats {
    unsigned frames, injected_frames, interventions, manual_overrides, suspends;
    unsigned reasons[UDE_R_COUNT];
    unsigned threats[UDE_T_COUNT];
    unsigned taps, impossible, recover_presses;
    unsigned recipe_fires[UDE_K_COUNT], recipe_frames, claims;
} UdeStats;

typedef struct UdeState {
    int mode;                  /* UdeMode, for the indicator */
    int last_manual;           /* frames since the last manual action button (saturates) */
    int holding;               /* frames the current injected guard has lasted */
    int tail;                  /* tail frames left */
    int cooldown;              /* cooldown frames left */
    int suspend;               /* suspension frames left */
    int not_accepted;          /* consecutive refused interventions */
    int last_threat;
    int last_reason;
    int tap_left;              /* frames of the current tap still to send */
    int since_press;           /* frames since the engine's last press edge (saturates) */
    int last_injected;         /* GUARD was injected on the previous frame */
    int recover_phase;         /* alternates 1 / 0 while recovering: a press only every other frame */
    int claim;                 /* v5: the player's own action owns the fighter (manual_claim) */
    int recipe_ran, recipe_edge, last_rresult;   /* v5: this update's recipe step (finish() reads them) */
    UdeRecipeState rs;         /* v5: the recipe engine's plan */
    UdeStats stats;
} UdeState;

typedef struct UdeDecision {
    unsigned buttons;          /* the word to use: physical | (maybe) GUARD */
    int injected;              /* 1 when GUARD was added this frame */
    int threat;                /* UdeThreat seen */
    int reason;                /* UdeReason when not injected */
    int response;              /* UdeResponse selected */
    int confidence;            /* UdeConfidence */
    int press_edge;            /* this frame's word starts a press (guard, or the recipe's own edge) */
    int origin;                /* v5: who decided: UdeOrigin */
    int recipe_kind;           /* v5: UdeRecipeKind of the packet (0 none / plain guard) */
    int recipe_result;         /* v5: UdeRecipeResult, -1 when the recipe engine did not run */
} UdeDecision;

enum UdeOrigin { UDE_O_NONE = 0, UDE_O_V4_HOLD = 1, UDE_O_V4_TAP = 2, UDE_O_RECOVER = 3, UDE_O_RECIPE = 4 };

void Ude_DefaultConfig(UdeConfig *c);
void Ude_Reset(UdeState *s);
/* The whole engine: returns the button word for this frame. `physical` is the word built from the pad. */
unsigned Ude_Step(UdeState *s, const UdeConfig *c, const UdeSnapshot *snap, unsigned physical, UdeDecision *out);
int Ude_ClassifyThreat(const UdeConfig *c, const UdeSnapshot *snap);
int Ude_IsFreeAction(int action);
int Ude_IsGuardAction(int action);
const char *Ude_ReasonName(int r);
const char *Ude_ThreatName(int t);
const char *Ude_ResponseName(int r);
int Ude_EtaFrames(const UdeSnapshot *snap, int threat);
void Ude_SetEta(int action, int frames);
void Ude_ClearEta(void);

#ifdef __cplusplus
}
#endif
#endif
