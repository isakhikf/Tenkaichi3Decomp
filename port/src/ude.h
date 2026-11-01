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

enum UdeThreat {           /* also the values of kUdeThreatByAction (generated) */
    UDE_T_NONE = 0,
    UDE_T_MELEE = 1,       /* table-driven attack actions 0x70..0xAD */
    UDE_T_RUSH = 2,        /* rush sequences 0x12D..0x12F, 0x139..0x13B */
    UDE_T_TECHNIQUE = 3,   /* Blast 2 / Ultimate actions 0x105.. */
    UDE_T_SKILL = 4,       /* Blast 1 skill actions: never defended (mostly self effects) */
    UDE_T_CLASH = 5,       /* never touched: won by mashing */
    UDE_T_BLAST = 6        /* a projectile flying at the player (BtlCharApi_FindIncomingBlast) */
};

enum UdeMode { UDE_OFF = 0, UDE_ON = 1, UDE_SUSPENDED = 2 };

enum UdeReason {           /* why the engine did not (or could not) intervene this frame */
    UDE_R_NONE = 0, UDE_R_DISABLED, UDE_R_ONLINE, UDE_R_REPLAY, UDE_R_CPU_SIDE, UDE_R_NOT_FIGHTING, UDE_R_TIME_STOP,
    UDE_R_BAD_SNAPSHOT, UDE_R_NO_THREAT, UDE_R_MANUAL, UDE_R_MOVING, UDE_R_BUSY, UDE_R_OUT_OF_REACH,
    UDE_R_BEHIND, UDE_R_NO_KI, UDE_R_HOLD_CAP, UDE_R_NOT_ACCEPTED, UDE_R_COOLDOWN, UDE_R_COUNT
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
} UdeConfig;

typedef struct UdeStats {
    unsigned frames, injected_frames, interventions, manual_overrides, suspends;
    unsigned reasons[UDE_R_COUNT];
    unsigned threats[7];
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
    UdeStats stats;
} UdeState;

typedef struct UdeDecision {
    unsigned buttons;          /* the word to use: physical | (maybe) GUARD */
    int injected;              /* 1 when GUARD was added this frame */
    int threat;                /* UdeThreat seen */
    int reason;                /* UdeReason when not injected */
} UdeDecision;

void Ude_DefaultConfig(UdeConfig *c);
void Ude_Reset(UdeState *s);
/* The whole engine: returns the button word for this frame. `physical` is the word built from the pad. */
unsigned Ude_Step(UdeState *s, const UdeConfig *c, const UdeSnapshot *snap, unsigned physical, UdeDecision *out);
int Ude_ClassifyThreat(const UdeConfig *c, const UdeSnapshot *snap);
int Ude_IsFreeAction(int action);
int Ude_IsGuardAction(int action);
const char *Ude_ReasonName(int r);
const char *Ude_ThreatName(int t);

#ifdef __cplusplus
}
#endif
#endif
