/* UDE-DB1: pure scalar offline policy. No game headers, allocation or I/O.
   State belongs to this candidate, never to a fighter or synchronized game world. */
#ifndef UDE_DB_H
#define UDE_DB_H
#include "ude.h"

#ifdef __cplusplus
extern "C" {
#endif

enum UdeDbRecipe {
    UDE_DB_NONE, UDE_DB_GUARD_HOLD, UDE_DB_GUARD_HIGH_LOW, UDE_DB_VANISH_EVADE,
    UDE_DB_Z_COUNTER, UDE_DB_SONIC_SWAY, UDE_DB_VANISHING_STEP, UDE_DB_VANISH_BEHIND,
    UDE_DB_BLAST_EVADE, UDE_DB_RUSH_EVADE, UDE_DB_THROW_BREAK, UDE_DB_KI_DEFLECT,
    UDE_DB_PERFECT_DEFLECT, UDE_DB_REFLECT_ABSORB, UDE_DB_SIDESTEP, UDE_DB_DRAGON_DASH_ESCAPE,
    UDE_DB_EMERGENCY_BLASTER_WAVE, UDE_DB_ARMOR_REPEL, UDE_DB_SKILL_DEFENSE, UDE_DB_RECOVERY,
    UDE_DB_CLASH_MASH, UDE_DB_CLASH_PROMPT, UDE_DB_RECIPE_COUNT
};

/* Existing safety reason IDs are reused exactly; additional DB reasons follow. */
enum UdeDbReason {
    UDE_DB_R_NOT_APPLICABLE = UDE_R_COUNT, UDE_DB_R_FIRST_HIT_UNKNOWN,
    UDE_DB_R_LATER_HIT, UDE_DB_R_CAPABILITY, UDE_DB_R_ANIMATION,
    UDE_DB_R_RECORD_UNKNOWN, UDE_DB_R_RECORD_MISMATCH, UDE_DB_R_NOT_REDIRECTABLE,
    UDE_DB_R_RELEASE, UDE_DB_R_PRESS_GAP, UDE_DB_R_POSE_TOO_LATE,
    UDE_DB_R_UNBLOCKABLE, UDE_DB_R_SEQUENCE_GAP, UDE_DB_R_FORBIDDEN, UDE_DB_R_LOWER_PRIORITY,
    UDE_DB_R_COUNT
};

typedef struct UdeDbConfig {
    int enabled;
    unsigned kinds;
    int tap_lead100, min_press_gap, manual_grace;
    int recover, dragon_escape, sidestep, z_counter;
    int hold_cap, min_ki;
} UdeDbConfig;

typedef struct UdeDbAttack {
    unsigned flags;
    signed char guard[4]; /* raw A/B/C/E. Ability 0x40 can only improve 1 -> 0. */
    signed char priority;
} UdeDbAttack;

typedef struct UdeDbTechnique {
    const char *id;
    unsigned buttons;
    int window, pulse, automatic;
} UdeDbTechnique;
typedef struct UdeDbWindow { int floor, input_condition, flag5, early_reset; } UdeDbWindow;

typedef struct UdeDbSnapshot {
    UdeSnapshot battle; /* Existing snapshot + existing recipe[] scalars. */
    int opp_character;  /* BtlCharApi_GetChara(opponent obj); NOT active member index. */
    int projectile_hits; /* -1 unknown/no observed projectile; exact native task hit count. */
    int own_flag41;      /* -1 unknown; exact native flag, never inferred from animation. */
    int own_motion;
    /* Policy view filled by UdeDb_Step; also usable directly by deterministic unit tests. */
    UdeDbConfig config;
    unsigned physical, frame, previous_added;
    int manual_age, press_age, manual_claim, release_due, holding;
} UdeDbSnapshot;

typedef struct UdeDbDecision {
    int recipe, reason, threat;
    int press_frame; /* absolute 30 Hz update, -1 when no scheduled/known press */
    unsigned added, buttons;
    int press_edge;
    int rejected[UDE_DB_RECIPE_COUNT]; /* reason each candidate was not selected */
} UdeDbDecision;

typedef struct UdeDbState {
    unsigned previous_added, last_frame;
    int have_frame, manual_age, press_age, claim, claim_wait, release_due, holding;
    unsigned frames, interventions, taps, manual_overwrites;
} UdeDbState;

void UdeDb_DefaultConfig(UdeDbConfig *config);
void UdeDb_Reset(UdeDbState *state);
UdeDbDecision UdeDb_Choose(const UdeDbSnapshot *snapshot);
unsigned UdeDb_Step(UdeDbState *state, const UdeDbConfig *config,
                    const UdeDbSnapshot *snapshot, unsigned physical,
                    unsigned frame, UdeDbDecision *decision);
const UdeDbAttack *UdeDb_Attack(int character_id, int attack_id);
const char *UdeDb_RecipeName(int recipe);
const char *UdeDb_ReasonName(int reason);
const char *UdeDb_SourceSha256(void);
int UdeDb_Threat(const UdeDbSnapshot *snapshot);

#ifdef __cplusplus
}
#endif
#endif
