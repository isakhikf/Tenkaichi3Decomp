/*
 * UDE game adapter (goes to port/src/ude_game.c in Tenkaichi3Decomp; compiled with the port's own files).
 *
 * Called from BtlInput_BuildRecord (src/battle/btl_input.c, under #ifdef PORT) for a fighter whose input comes from
 * a pad, after key config and double taps, before the command word is built. It reads the fight through the game's
 * scalar accessors only (the ones the CPU opponent's AI uses; no game pointer crosses into this file, as in
 * a11y_game.c) and returns the button word, with BTLB_GUARD added when the engine decides so.
 *
 * Off-line only: online (Port_NetActive), during a replay, for a CPU-controlled side, and outside the fight state the
 * word is returned unchanged. Nothing is written to the game's memory.
 *
 * Settings (bt3_settings.txt, `name=number`):
 *   ude_enabled      1   master switch; F7 toggles it in game and saves it
 *   ude_player       0   0 = player 1 only, 1 = player 2 only, 2 = both (local versus)
 *   ude_melee / ude_rush / ude_technique / ude_blast   1   which threats to guard
 *   ude_grace        4   frames after a manual action button before the engine may act
 *   ude_moving       0   1: also guard while a direction is held (then movement stops during the threat)
 *   ude_tail 3, ude_cap 90, ude_cooldown 20, ude_accept 8, ude_suspend 60, ude_min_ki 0
 *   ude_log          0   1: append threat/decision lines to ude_log.csv (for tools/validation/import_log.py)
 */
#include <stdio.h>
#include <string.h>
#include "ude.h"

extern int Port_Setting(const char *name, int def);        /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);
extern int Port_NetActive(void);                           /* gs/net.c */
extern int BattleReplay_IsActive(void);                    /* src/battle/battle_load.c */
extern int BtlCharApi_GetObjIdOfPlayer(int player);        /* src/battle/btl_capi_a.c */
extern int BtlCharApi_GetOpponentObjId(int objId);
extern int BtlCharApi_IsClose(int objId);                  /* fighter flag 0x13 */
extern int BtlCharApi_IsInputInjected(int objId);          /* src/battle/btl_char_api.c */
extern int BtlCharApi_CheckUnkCAC(int objId);
extern int BtlCharApi_GetUnk974(int objId);                /* motion id */
extern int BtlCharApi_GetAction(int objId);                /* src/battle/btl_capi_b.c: fighter +0x948 */
extern int BtlCharApi_FindIncomingBlast(int objId, int mode);
extern int BtlCharApi_GetOppSkillKind(int objId);
extern int BtlCharApi_GetKi(int objId);
extern int BtlCharApi_GetHp(int objId);
extern int BtlChar_GetFrame(void);
extern int gPortResim;                                     /* gs/state.c: blanks run again (rollback) */

static UdeConfig sCfg;
static UdeState sState[2];
static int sInit;
static volatile int sIndicator = -1;   /* read by the renderer thread: -1 not in a fight, else UdeMode of player 1 */
static volatile int sToggle;
static volatile unsigned sStamp;     /* +1 per call: the indicator hides when it stops moving (fight over) */           /* set by the renderer thread (F7), applied on the game thread */
static FILE *sLog;
static int sLogOn, sPlayers;

static void load(void) {
    Ude_DefaultConfig(&sCfg);
    sCfg.enabled = Port_Setting("ude_enabled", 1);
    sCfg.guard_melee = Port_Setting("ude_melee", 1);
    sCfg.guard_rush = Port_Setting("ude_rush", 1);
    sCfg.guard_technique = Port_Setting("ude_technique", 1);
    sCfg.guard_blast = Port_Setting("ude_blast", 1);
    sCfg.manual_grace = Port_Setting("ude_grace", 4);
    sCfg.allow_while_moving = Port_Setting("ude_moving", 0);
    sCfg.hold_tail = Port_Setting("ude_tail", 3);
    sCfg.hold_cap = Port_Setting("ude_cap", 90);
    sCfg.cooldown = Port_Setting("ude_cooldown", 20);
    sCfg.accept_frames = Port_Setting("ude_accept", 8);
    sCfg.suspend_frames = Port_Setting("ude_suspend", 60);
    sCfg.min_ki = Port_Setting("ude_min_ki", 0);
    sPlayers = Port_Setting("ude_player", 0);
    sLogOn = Port_Setting("ude_log", 0);
    Ude_Reset(&sState[0]);
    Ude_Reset(&sState[1]);
    sInit = 1;
}

/* Renderer thread (gs_gpu.c / gs_gl.c, F7): only a request; the game thread applies it. */
void PortUde_RequestToggle(void) { sToggle = 1; }
/* Renderer thread (ui.cpp): what the corner indicator shows. -1 = nothing (not in a fight or not loaded). */
int PortUde_Indicator(void) { return sIndicator; }
unsigned PortUde_Stamp(void) { return sStamp; }
int PortUde_Enabled(void) { return sInit ? sCfg.enabled : Port_Setting("ude_enabled", 1); }

static int wanted(int player) {
    return player >= 0 && player < 2 && (sPlayers == 2 || sPlayers == player);
}

unsigned int PortUde_Filter(int player, int active, unsigned int buttons) {
    UdeSnapshot s;
    UdeDecision d;
    int me, opp;
    unsigned int out;

    if (!sInit) load();
    if (sToggle) {
        sToggle = 0;
        sCfg.enabled = !sCfg.enabled;
        Port_SettingSave("ude_enabled", sCfg.enabled);
        Port_SettingsWrite();
        fprintf(stderr, "bt3: defense assist %s\n", sCfg.enabled ? "on" : "off");
    }
    sStamp++;
    if (!wanted(player)) return buttons;
    if (gPortResim) return buttons;     /* never during a rollback re-run (online is refused anyway) */

    memset(&s, 0, sizeof s);
    me = BtlCharApi_GetObjIdOfPlayer(player);
    opp = BtlCharApi_GetOpponentObjId(me);
    s.valid = me != opp;
    s.online = Port_NetActive();
    s.replay = BattleReplay_IsActive();
    s.cpu_side = BtlCharApi_IsInputInjected(me);
    s.fighting = active;
    s.time_stopped = 0;                 /* folded into `active` by the hook */
    s.my_action = BtlCharApi_GetAction(me);
    s.opp_action = BtlCharApi_GetAction(opp);
    s.opp_attacking = BtlCharApi_CheckUnkCAC(opp);
    s.opp_skill_kind = BtlCharApi_GetOppSkillKind(me);
    s.incoming_blast = BtlCharApi_FindIncomingBlast(me, 0) != -1;
    s.close = BtlCharApi_IsClose(me);
    s.behind = -1;                      /* not read in v1: needs positions (pointer accessors); guard still tried */
    s.my_ki = BtlCharApi_GetKi(me);
    s.my_hp = BtlCharApi_GetHp(me);

    out = Ude_Step(&sState[player], &sCfg, &s, buttons, &d);
    if (player == (sPlayers == 1 ? 1 : 0)) sIndicator = active ? sState[player].mode : -1;

    if (sLogOn && (d.injected || d.threat != 0 || s.opp_attacking)) {
        if (sLog == NULL) {
            sLog = fopen("ude_log.csv", "a");
            if (sLog != NULL) {
                fprintf(sLog, "frame,player,my_action,my_motion,opp_action,opp_motion,threat,injected,reason,close,blast,opp_attacking,skill_kind,hp,ki,buttons\n");
            }
        }
        if (sLog != NULL) {
            fprintf(sLog, "%d,%d,%d,%d,%d,%d,%s,%d,%s,%d,%d,%d,%d,%d,%d,0x%X\n", BtlChar_GetFrame(), player, s.my_action,
                    BtlCharApi_GetUnk974(me), s.opp_action, BtlCharApi_GetUnk974(opp), Ude_ThreatName(d.threat), d.injected,
                    Ude_ReasonName(d.reason), s.close, s.incoming_blast, s.opp_attacking, s.opp_skill_kind, s.my_hp, s.my_ki, buttons);
        }
    }
    return out;
}
