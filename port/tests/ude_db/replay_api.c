/* Scalar ABI for offline log replay via Python ctypes. Test-only, not shipped
   into the game. All state belongs to this host process. */
#include "ude_db.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static UdeDbState states[2];
API int UdeDbReplay_Version(void) { return UDE_X_COUNT; }
API void UdeDbReplay_Reset(void) { UdeDb_Reset(&states[0]);UdeDb_Reset(&states[1]); }
API void UdeDbReplay_State(int player,unsigned *v,int restore) {
    UdeDbState *s=&states[player&1];
    if(restore) {
        s->previous_added=v[0];s->last_frame=v[1];s->have_frame=(int)v[2];s->manual_age=(int)v[3];s->press_age=(int)v[4];
        s->claim=(int)v[5];s->claim_wait=(int)v[6];s->release_due=(int)v[7];s->holding=(int)v[8];
        s->frames=v[9];s->interventions=v[10];s->taps=v[11];s->manual_overwrites=v[12];
    } else {
        v[0]=s->previous_added;v[1]=s->last_frame;v[2]=(unsigned)s->have_frame;v[3]=(unsigned)s->manual_age;v[4]=(unsigned)s->press_age;
        v[5]=(unsigned)s->claim;v[6]=(unsigned)s->claim_wait;v[7]=(unsigned)s->release_due;v[8]=(unsigned)s->holding;
        v[9]=s->frames;v[10]=s->interventions;v[11]=s->taps;v[12]=s->manual_overwrites;
    }
}
API void UdeDbReplay_Step(const int *h,const int *r,const int *config,unsigned physical,unsigned *result,int *rejected) {
    UdeDbSnapshot s={0};UdeDbConfig c;UdeDbDecision d;UdeSnapshot *b=&s.battle;int i;
    UdeDb_DefaultConfig(&c);
    c.enabled=config[0];c.kinds=(unsigned)config[1];c.tap_lead100=config[2];c.min_press_gap=config[3];c.manual_grace=config[4];
    c.recover=config[5];c.dragon_escape=config[6];c.sidestep=config[7];c.z_counter=config[8];c.hold_cap=config[9];c.min_ki=config[10];
    s.frame=(unsigned)h[0];s.opp_character=h[2];s.projectile_hits=h[3];s.own_flag41=h[4];s.own_motion=h[5];
    b->my_hp=h[6];b->close=h[7];b->my_action_frame=h[8];b->opp_action_frame=h[9];b->my_request_pending=h[10];b->recover_ok=h[11];
    b->online=h[12];b->replay=h[13];b->cpu_side=h[14];b->fighting=h[15];b->time_stopped=h[16];b->valid=h[17];b->have_ext=h[18];b->have_recipe=h[19];
    for(i=0;i<UDE_X_COUNT;i++)b->recipe[i]=r[i];
    b->my_action=r[UDE_X_MY_ACT];b->opp_action=r[UDE_X_OPP_ACT];b->anim_flags=r[UDE_X_OWN_FLAGS];b->game_free=r[UDE_X_FREE];
    b->behind=r[UDE_X_BACK];b->flag5=r[UDE_X_FLAG5];b->no_evade=r[UDE_X_NO_EVADE];b->opp_attack_id=r[UDE_X_ATK];
    b->opp_attack_flags=r[UDE_X_ATK_FLAGS];b->opp_attacking=r[UDE_X_MELEE];b->my_ki=r[UDE_X_MY_KI];
    b->time_stopped|=r[UDE_X_STOPPED];for(i=0;i<6;i++)b->win[i]=r[UDE_X_W1068+i];
    UdeDb_Step(&states[h[1]&1],&c,&s,physical,(unsigned)h[0],&d);
    result[0]=(unsigned)d.recipe;result[1]=(unsigned)d.reason;result[2]=(unsigned)d.threat;result[3]=(unsigned)d.press_frame;
    result[4]=d.added;result[5]=d.buttons;result[6]=(unsigned)d.press_edge;
    for(i=0;i<UDE_DB_RECIPE_COUNT;i++)rejected[i]=d.rejected[i];
}
