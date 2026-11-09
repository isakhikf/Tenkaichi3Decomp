/* UDE-DB1 adapter. The only gameplay output is the returned battle-button word
   at BtlInput_BuildRecord, before native command construction and replay.
   BtlCharApi_SetInjectedInput remains untouched: enabling the CPU injection flag
   would bypass physical pad sampling, so this adapter does NOT set that flag. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ude_db.h"

extern int Port_Setting(const char *, int);
extern void Port_SettingSave(const char *, int);
extern void Port_SettingsWrite(void);
extern int Port_NetActive(void), BattleReplay_IsActive(void), gPortResim;
extern int BtlCharApi_GetObjIdOfPlayer(int), BtlCharApi_GetOpponentObjId(int);
extern int BtlCharApi_IsInputInjected(int), BtlCharApi_IsClose(int);
extern int BtlCharApi_GetChara(int), BtlCharApi_GetUnk974(int), BtlCharApi_GetHp(int);
extern int BtlHit_PortUdeQuery(int, int), BtlHit_PortUdeRecipe(int, int), BtlHit_PortUdeDbQuery(int, int);
extern int BtlChar_GetFrame(void);
extern unsigned PortUde_Filter(int, int, unsigned);
extern int PortUde_Indicator(void);
extern unsigned PortUde_Stamp(void);

static UdeDbConfig sDbConfig;
static UdeDbState sDbState[2];
static int sDbInit, sDbPlayer, sDbLogging, sDbCharacter[2] = {-1, -1};
static volatile int sDbToggle, sDbMode = -1;
static volatile unsigned sDbStamp;
static FILE *sDbLog;
static char sDbLogBuffer[65536];

/* Drop only this engine's packet. A pause/toggle must not erase the physical
   owner's claim or allow a second press before the 30-update limit. */
static void cancel_packet(UdeDbState *s) {
    s->previous_added=0; s->release_due=0; s->holding=0;
}

static void close_log(void) { if (sDbLog) { fclose(sDbLog); sDbLog = NULL; } }
static void initialize(void) {
    UdeDb_DefaultConfig(&sDbConfig);
    sDbConfig.enabled = Port_Setting("ude_db_enabled", 0) != 0;
    sDbConfig.tap_lead100 = Port_Setting("ude_db_tap_lead", 1) * 100;
    sDbConfig.manual_grace = Port_Setting("ude_db_grace", 4);
    sDbConfig.recover = Port_Setting("ude_db_recover", 0) != 0;
    sDbConfig.sidestep = Port_Setting("ude_db_sidestep", 0) != 0;
    sDbConfig.z_counter = Port_Setting("ude_db_z", 0) != 0;
    sDbConfig.hold_cap = Port_Setting("ude_db_hold_cap", 90);
    sDbConfig.min_ki = Port_Setting("ude_db_min_ki", 0);
    sDbPlayer = Port_Setting("ude_db_player", 0);
    sDbLogging = Port_Setting("ude_db_log", 0) != 0;
    UdeDb_Reset(&sDbState[0]); UdeDb_Reset(&sDbState[1]);
    sDbInit = 1;
}
void PortUdeDb_RequestToggle(void) { sDbToggle = 1; }
int PortUdeDb_Active(void) { return sDbInit ? sDbConfig.enabled : Port_Setting("ude_db_enabled", 0) != 0; }
int PortUdeDb_Indicator(void) { return PortUdeDb_Active() ? sDbMode : PortUde_Indicator(); }
unsigned PortUdeDb_Stamp(void) { return sDbStamp + PortUde_Stamp(); }

static void log_frame(int player, const UdeDbSnapshot *s, const UdeDbDecision *d, const UdeDbState *before, unsigned physical,
                      int evade, int guard, int outcome) {
    int i;
    if (!sDbLogging) return;
    if (!sDbLog) {
        sDbLog = fopen("ude_db_log.jsonl", "a");
        if (!sDbLog) return;
        setvbuf(sDbLog, sDbLogBuffer, _IOFBF, sizeof sDbLogBuffer);
        atexit(close_log);
    }
    fprintf(sDbLog,"{\"schema\":1,\"candidate\":\"UDE-DB1\",\"frame\":%u,\"player\":%d,\"opp_character\":%d,\"projectile_hits\":%d,\"own_flag41\":%d,\"own_motion\":%d,\"physical\":%u,\"output\":%u,\"added\":%u,\"recipe\":%d,\"reason\":%d,\"press_edge\":%d,\"hp\":%d,\"close\":%d,\"my_action_frame\":%d,\"opp_action_frame\":%d,\"pending\":%d,\"recover_ok\":%d,\"observed_evade_flags\":%d,\"observed_guard_flags\":%d,\"observed_outcome\":%d,\"recipe_snapshot\":[",
            s->frame,player,s->opp_character,s->projectile_hits,s->own_flag41,s->own_motion,
            physical,d->buttons,d->added,d->recipe,d->reason,d->press_edge,s->battle.my_hp,s->battle.close,
            s->battle.my_action_frame,s->battle.opp_action_frame,s->battle.my_request_pending,s->battle.recover_ok,evade,guard,outcome);
    for (i=0;i<UDE_X_COUNT;i++) fprintf(sDbLog,"%s%d",i?",":"",s->battle.recipe[i]);
    fputs("],\"rejected\":[",sDbLog);
    for (i=0;i<UDE_DB_RECIPE_COUNT;i++) fprintf(sDbLog,"%s%d",i?",":"",d->rejected[i]);
    fprintf(sDbLog,"],\"config\":[%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d],\"state_before\":[%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u]}\n",
            sDbConfig.enabled,sDbConfig.kinds,sDbConfig.tap_lead100,sDbConfig.min_press_gap,sDbConfig.manual_grace,
            sDbConfig.recover,sDbConfig.dragon_escape,sDbConfig.sidestep,sDbConfig.z_counter,sDbConfig.hold_cap,sDbConfig.min_ki,
            before->previous_added,before->last_frame,before->have_frame,before->manual_age,before->press_age,
            before->claim,before->claim_wait,before->release_due,before->holding,
            before->frames,before->interventions,before->taps,before->manual_overwrites);
}

unsigned PortUdeDb_Filter(int player, int active, unsigned physical) {
    UdeDbSnapshot s;
    UdeDbDecision decision;
    UdeDbState before;
    UdeSnapshot *b = &s.battle;
    int me, opp, i, own, frame;
    unsigned out;
    if (!sDbInit) initialize();
    if (sDbToggle) {
        sDbToggle = 0; sDbConfig.enabled = !sDbConfig.enabled;
        Port_SettingSave("ude_db_enabled", sDbConfig.enabled); Port_SettingsWrite();
        cancel_packet(&sDbState[0]); cancel_packet(&sDbState[1]);
        fprintf(stderr,"bt3: UDE-DB1 %s (offline, physical input priority)\n",sDbConfig.enabled?"on":"off");
    }
    sDbStamp++;
    if (!sDbConfig.enabled) return PortUde_Filter(player,active,physical);
    /* Refuse before touching any fighter/roster/frame getter. No legacy fallthrough
       while DB1 owns the path, including for the other local/CPU side. */
    if (player < 0 || player > 1 || (sDbPlayer != 2 && sDbPlayer != player)) return physical;
    if (!active || Port_NetActive() || BattleReplay_IsActive() || gPortResim) {
        sDbMode = active ? UDE_SUSPENDED : -1;
        cancel_packet(&sDbState[player]);
        if (sDbLog) fflush(sDbLog);
        return physical;
    }
    if (BtlHit_PortUdeRecipe(player,-1) != 1) { sDbMode=UDE_SUSPENDED; return physical; }
    memset(&s,0,sizeof s);
    for (i=0;i<UDE_X_COUNT;i++) b->recipe[i]=BtlHit_PortUdeRecipe(player,i);
    me=BtlCharApi_GetObjIdOfPlayer(player); opp=BtlCharApi_GetOpponentObjId(me);
    if (me == opp || BtlCharApi_IsInputInjected(me)) { sDbMode=UDE_SUSPENDED; return physical; }
    own=BtlCharApi_GetChara(me);
    if (sDbCharacter[player]!=own) { cancel_packet(&sDbState[player]); sDbCharacter[player]=own; }
    b->valid=1; b->have_ext=1; b->have_recipe=1; b->fighting=1;
    b->my_action=b->recipe[UDE_X_MY_ACT]; b->opp_action=b->recipe[UDE_X_OPP_ACT];
    b->anim_flags=b->recipe[UDE_X_OWN_FLAGS]; b->game_free=b->recipe[UDE_X_FREE];
    b->time_stopped=b->recipe[UDE_X_STOPPED]; b->behind=b->recipe[UDE_X_BACK];
    b->flag5=b->recipe[UDE_X_FLAG5]; b->no_evade=b->recipe[UDE_X_NO_EVADE];
    b->opp_attack_id=b->recipe[UDE_X_ATK]; b->opp_attack_flags=b->recipe[UDE_X_ATK_FLAGS];
    b->opp_attacking=b->recipe[UDE_X_MELEE]; b->incoming_blast=(b->recipe[UDE_X_INC]&UDE_INC_HIT)!=0;
    b->my_ki=b->recipe[UDE_X_MY_KI]; b->my_hp=BtlCharApi_GetHp(me); b->close=BtlCharApi_IsClose(me);
    b->my_action_frame=BtlHit_PortUdeQuery(player,6); b->opp_action_frame=BtlHit_PortUdeQuery(player,8);
    b->my_request_pending=BtlHit_PortUdeQuery(player,7)!=-1; b->recover_ok=BtlHit_PortUdeQuery(player,23);
    for (i=0;i<6;i++) b->win[i]=b->recipe[UDE_X_W1068+i];
    s.opp_character=BtlCharApi_GetChara(opp); s.own_motion=BtlCharApi_GetUnk974(me);
    s.own_flag41=BtlHit_PortUdeDbQuery(player,0); s.projectile_hits=BtlHit_PortUdeDbQuery(player,1);
    frame=BtlChar_GetFrame(); s.frame=(unsigned)frame;
    before=sDbState[player];
    out=UdeDb_Step(&sDbState[player],&sDbConfig,&s,physical,(unsigned)frame,&decision);
    sDbMode=decision.reason==UDE_R_TIME_STOP || decision.reason==UDE_R_BAD_SNAPSHOT ? UDE_SUSPENDED : UDE_ON;
    if (sDbLogging) log_frame(player,&s,&decision,&before,physical,BtlHit_PortUdeQuery(player,17),BtlHit_PortUdeQuery(player,18),BtlHit_PortUdeQuery(player,25));
    return physical | out; /* preserve every physical bit even if a future core regresses */
}
