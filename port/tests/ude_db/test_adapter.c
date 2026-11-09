/* Link-time boundary test: only scalar read APIs, settings and old delegation
   exist here. Any new call to a game mutator would fail to link this adapter. */
#include <stdio.h>
#include <string.h>
#include "ude_db.h"
extern unsigned PortUdeDb_Filter(int,int,unsigned);
extern void PortUdeDb_RequestToggle(void);

static int online,replay,cpu,getters,legacy,frame=100,extra_reads,failed,total;
static int values[UDE_X_COUNT];
int gPortResim;
int Port_Setting(const char *s,int fallback) { (void)s;return fallback; }
void Port_SettingSave(const char *s,int x) { (void)s;(void)x; }
void Port_SettingsWrite(void) {}
int Port_NetActive(void) {return online;}
int BattleReplay_IsActive(void) {return replay;}
int BtlCharApi_GetObjIdOfPlayer(int p) {getters++;return p+10;}
int BtlCharApi_GetOpponentObjId(int o) {getters++;return o==10?11:10;}
int BtlCharApi_IsInputInjected(int o) {(void)o;getters++;return cpu;}
int BtlCharApi_IsClose(int o) {(void)o;getters++;return 1;}
int BtlCharApi_GetChara(int o) {getters++;return o==11?54:0;}
int BtlCharApi_GetUnk974(int o) {(void)o;getters++;return 0;}
int BtlCharApi_GetHp(int o) {(void)o;getters++;return 40000;}
int BtlChar_GetFrame(void) {getters++;return frame;}
int BtlHit_PortUdeRecipe(int p,int n) {(void)p;getters++;return n==-1?1:values[n];}
int BtlHit_PortUdeQuery(int p,int n) {(void)p;getters++;return n==7?-1:n==23?1:0;}
int BtlHit_PortUdeDbQuery(int p,int n) {(void)p;extra_reads++;return n==0?0:-1;}
unsigned PortUde_Filter(int p,int a,unsigned b) {(void)p;(void)a;legacy++;return b;}
int PortUde_Indicator(void) {return 0;}
unsigned PortUde_Stamp(void) {return 0;}
static void check(const char *name,int ok){total++;failed+=!ok;printf("adapter_%s,%s\n",name,ok?"PASS":"FAIL");}
int main(void) {
    int i,id,mark;
    unsigned out;
    check("default_delegates_to_unchanged_legacy",PortUdeDb_Filter(0,1,8)==8&&legacy==1);
    PortUdeDb_RequestToggle();online=1;mark=getters;
    check("online_no_game_reads",PortUdeDb_Filter(0,1,0x123)==0x123&&getters==mark&&legacy==1);
    online=0;replay=1;mark=getters;
    check("replay_no_game_reads",PortUdeDb_Filter(0,1,8)==8&&getters==mark);
    replay=0;gPortResim=1;mark=getters;
    check("resim_no_game_reads",PortUdeDb_Filter(0,1,8)==8&&getters==mark);
    gPortResim=0;mark=getters;
    check("inactive_no_roster_or_frame_reads",PortUdeDb_Filter(0,0,8)==8&&getters==mark);
    check("other_side_no_game_reads",PortUdeDb_Filter(1,1,8)==8&&getters==mark);
    values[UDE_X_OK]=1;values[UDE_X_FREE]=1;values[UDE_X_MY_ACT]=0xB;
    values[UDE_X_OPP_ACT]=0x70;values[UDE_X_OPP_MEMBER]=1; /* Deliberately != character 54. */
    values[UDE_X_MELEE]=1;values[UDE_X_OWN_FLAGS]=0xB1;values[UDE_X_FLAG5]=1;
    values[UDE_X_DODGE_WIN]=1;values[UDE_X_FIRST_HIT]=1;values[UDE_X_MY_KI]=100000;
    for(i=0;i<6;i++)values[UDE_X_W1068+i]=i==3?-15:-30;
    for(id=11;id<163;id++)if((UdeDb_Attack(54,id)->flags&1)&&!(UdeDb_Attack(54,id)->flags&0x4000000))break;
    values[UDE_X_ATK]=id;values[UDE_X_ATK_FLAGS]=(int)UdeDb_Attack(54,id)->flags;
    cpu=1;out=PortUdeDb_Filter(0,1,8);check("cpu_unchanged",out==8&&legacy==1);
    cpu=0;out=PortUdeDb_Filter(0,1,0);check("opponent_object_character_indexes_table",out&1);
    check("exactly_two_additional_reads_per_valid_db1_frame",extra_reads==2);
    frame++;out=PortUdeDb_Filter(0,1,0x80);check("physical_direction_cancels",out==0x80);
    PortUdeDb_RequestToggle();check("toggle_off_restores_old_path",PortUdeDb_Filter(0,1,8)==8&&legacy==2);
    PortUdeDb_RequestToggle();frame++;
    check("toggle_does_not_erase_manual_grace_or_press_history",PortUdeDb_Filter(0,1,0)==0&&legacy==2);
    frame++;check("inactive_does_not_read_game_state",PortUdeDb_Filter(0,0,0)==0);
    frame++;check("resume_does_not_fabricate_a_fresh_press",PortUdeDb_Filter(0,1,0)==0);
    printf("adapter_total=%d failed=%d\n",total,failed);return failed?1:0;
}
