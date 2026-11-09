/* DB1 unit and temporal safety tests. Synthetic snapshots; no game outcomes. */
#include <stdio.h>
#include <string.h>
#include "ude_db.h"

static int total, failed;
static void check(const char *name,int good) {
    total++; failed += !good;
    printf("%s,%s\n",name,good?"PASS":"FAIL");
}
static int find_attack(unsigned set,unsigned clear,int minimum) {
    int i;
    for(i=minimum;i<163;i++) {
        const UdeDbAttack *a=UdeDb_Attack(0,i);
        if ((a->flags&set)==set && !(a->flags&clear) && a->guard[0]!=3) return i;
    }
    return -1;
}
static void attack(UdeDbSnapshot *s,int id) {
    const UdeDbAttack *a=UdeDb_Attack(0,id);
    s->battle.opp_attack_id=id; s->battle.opp_attack_flags=a?(int)a->flags:0;
    s->battle.recipe[UDE_X_ATK]=id;
}
static UdeDbSnapshot base(void) {
    UdeDbSnapshot s; int i;
    memset(&s,0,sizeof s); UdeDb_DefaultConfig(&s.config); s.config.enabled=1;
    s.manual_age=s.press_age=1000; s.frame=100;
    s.battle.valid=s.battle.have_ext=s.battle.have_recipe=s.battle.fighting=s.battle.game_free=1;
    s.battle.flag5=1; s.battle.anim_flags=0xB1; s.battle.my_action=0xB;
    s.battle.opp_action=0x70; s.battle.opp_attacking=1; s.battle.close=1;
    s.battle.my_ki=100000; s.battle.my_hp=40000;
    for(i=0;i<6;i++) s.battle.win[i]=i==3?-15:-30;
    s.battle.recipe[UDE_X_OK]=1; s.battle.recipe[UDE_X_FREE]=1;
    s.battle.recipe[UDE_X_MELEE]=1; s.battle.recipe[UDE_X_FIRST_HIT]=1;
    s.battle.recipe[UDE_X_DODGE_WIN]=1; s.battle.recipe[UDE_X_OPP_UNK2]=2;
    s.battle.recipe[UDE_X_CAPS]=0xA0; s.battle.recipe[UDE_X_DIST100]=10000;
    s.battle.recipe[UDE_X_D_LT_R]=1; s.battle.recipe[UDE_X_INC_ETA100]=100000;
    s.own_motion=0xEF; s.projectile_hits=-1; s.own_flag41=0;
    attack(&s,find_attack(1,0x4000000,11));
    return s;
}
static UdeDbSnapshot projectile(int technique) {
    UdeDbSnapshot s=base();
    s.battle.opp_action=technique?0x106:0xAE; s.battle.close=0;
    s.battle.recipe[UDE_X_MELEE]=0; s.battle.opp_attacking=0;
    s.battle.recipe[UDE_X_INC]=UDE_INC_HIT|(technique?(UDE_INC_TECH|UDE_INC_EVADABLE):UDE_INC_REDIRECT);
    s.battle.recipe[UDE_X_PROJ_FIRST]=1; s.battle.recipe[UDE_X_INC_ETA100]=100;
    s.projectile_hits=0;
    attack(&s,-1);
    return s;
}
static int selected(UdeDbSnapshot *s,int recipe) { return UdeDb_Choose(s).recipe==recipe; }
static int rejection(UdeDbSnapshot *s,int recipe,int reason) { return UdeDb_Choose(s).rejected[recipe]==reason; }

int main(void) {
    UdeDbSnapshot s;
    UdeDbDecision d;
    UdeDbState state;
    UdeDbConfig cfg;
    int i,kind,last_press=-100, errors=0,high=-1;
    unsigned rng=1234567,physical,out;
    UdeDb_DefaultConfig(&cfg);
    check("default_off",!cfg.enabled&&!cfg.recover&&!cfg.dragon_escape&&cfg.min_press_gap>=30);
    check("attack_bounds",!UdeDb_Attack(-1,0)&&!UdeDb_Attack(161,0)&&!UdeDb_Attack(0,163));
    s=base(); check("VANISH_EVADE",selected(&s,UDE_DB_VANISH_EVADE));
    s.previous_added=0x100001;s.battle.recipe[UDE_X_UNTIL100]=100;
    d=UdeDb_Choose(&s);check("held_guard_prepares_release",d.recipe==0&&d.reason==UDE_DB_R_RELEASE&&d.press_frame==101);
    s.battle.recipe[UDE_X_UNTIL100]=0;
    check("held_guard_is_not_falsely_counted_as_tap",!selected(&s,UDE_DB_VANISH_EVADE)&&rejection(&s,UDE_DB_VANISH_EVADE,UDE_R_RECIPE_EDGE));
    s.previous_added=0;check("released_guard_can_form_fresh_tap",selected(&s,UDE_DB_VANISH_EVADE));
    s.config.enabled=0; check("disabled_no_output",selected(&s,UDE_DB_NONE)&&UdeDb_Choose(&s).reason==UDE_R_DISABLED);
    s=base(); s.battle.online=1; check("online",UdeDb_Choose(&s).reason==UDE_R_ONLINE);
    s=base(); s.battle.replay=1; check("replay",UdeDb_Choose(&s).reason==UDE_R_REPLAY);
    s=base(); s.battle.cpu_side=1; check("cpu_side",UdeDb_Choose(&s).reason==UDE_R_CPU_SIDE);
    s=base(); s.battle.time_stopped=1; check("time_stop",UdeDb_Choose(&s).reason==UDE_R_TIME_STOP);
    s=base(); s.battle.recipe[UDE_X_STOPPED]=1; check("input_freeze",UdeDb_Choose(&s).reason==UDE_R_TIME_STOP);
    s=base(); s.battle.my_request_pending=1; check("pending_always_wins",UdeDb_Choose(&s).reason==UDE_R_REQUEST_PENDING);
    s=base(); s.manual_age=3; check("manual_grace",UdeDb_Choose(&s).reason==UDE_R_MANUAL);
    s=base(); s.manual_claim=1; check("manual_claim",UdeDb_Choose(&s).reason==UDE_R_MANUAL_ACTION);
    for(i=0;i<32;i++) {
        s=base(); s.physical=1u<<i; d=UdeDb_Choose(&s);
        if (((1u<<i)&~(0x100000u|0x78000000u|0x1000000u)) && (d.added || d.buttons!=s.physical)) errors++;
        if ((d.buttons&s.physical)!=s.physical) errors++;
    }
    check("every_physical_bit_preserved_and_deliberate_input_exclusive",errors==0);
    s=base(); s.battle.win[0]=-29; check("evade_cooldown",rejection(&s,UDE_DB_VANISH_EVADE,UDE_R_WINDOW_COOLING));
    check("fresh_hold_cannot_bypass_cooldown",selected(&s,UDE_DB_NONE));
    s=base(); s.battle.flag5=0; check("tap_no_lock_on",rejection(&s,UDE_DB_VANISH_EVADE,UDE_R_NOT_EVADABLE));
    s=base(); s.battle.recipe[UDE_X_UNTIL100]=101; check("tap_too_early",rejection(&s,UDE_DB_VANISH_EVADE,UDE_R_WAIT_TIMING));
    s=base(); s.battle.recipe[UDE_X_UNTIL100]=100000; check("unknown_eta_not_hit_active",rejection(&s,UDE_DB_VANISH_EVADE,UDE_R_WAIT_TIMING));
    s=base(); s.battle.recipe[UDE_X_FIRST_HIT]=0; check("later_hit_guard",selected(&s,UDE_DB_GUARD_HOLD));
    s.battle.have_recipe=0; check("missing_snapshot",UdeDb_Choose(&s).reason==UDE_R_BAD_SNAPSHOT);
    s=base(); s.opp_character=-1; check("unknown_character_not_member_zero",rejection(&s,UDE_DB_VANISH_EVADE,UDE_DB_R_RECORD_UNKNOWN));
    s=base(); s.battle.opp_attack_flags^=1; check("live_flags_must_match_database",rejection(&s,UDE_DB_VANISH_EVADE,UDE_DB_R_RECORD_MISMATCH));
    s=base(); s.config.kinds&=~(1u<<UDE_DB_VANISH_EVADE); s.config.z_counter=1;
    check("Z_COUNTER_opt_in",selected(&s,UDE_DB_Z_COUNTER));
    s.battle.recipe[UDE_X_UNTIL100]=100;check("Z_not_one_frame_early",rejection(&s,UDE_DB_Z_COUNTER,UDE_R_WAIT_TIMING));
    s.previous_added=0x10;d=UdeDb_Choose(&s);check("Z_releases_previously_held_up",d.recipe==0&&d.reason==UDE_DB_R_RELEASE);
    s=base(); s.battle.recipe[UDE_X_DODGE_WIN]=0; s.battle.anim_flags=0;
    check("illegal_animation_no_vanish",rejection(&s,UDE_DB_VANISH_EVADE,UDE_DB_R_ANIMATION));
    s=base(); attack(&s,find_attack(1|0x4000000,0,11)); s.battle.recipe[UDE_X_UNTIL100]=100;
    check("extra_level_waits_for_contact",rejection(&s,UDE_DB_VANISH_EVADE,UDE_R_WAIT_TIMING));
    s.battle.recipe[UDE_X_UNTIL100]=0; check("extra_level_at_contact",selected(&s,UDE_DB_VANISH_EVADE));
    s=base(); attack(&s,0); s.config.kinds&=~(1u<<UDE_DB_VANISH_EVADE);
    check("SONIC_SWAY",selected(&s,UDE_DB_SONIC_SWAY));
    s.battle.behind=1; check("sway_from_behind_rejected",rejection(&s,UDE_DB_SONIC_SWAY,UDE_R_BEHIND));
    s=base(); attack(&s,0); s.battle.recipe[UDE_X_CAPS]&=~0x80;
    check("sway_capability",rejection(&s,UDE_DB_SONIC_SWAY,UDE_DB_R_CAPABILITY));
    s=base(); attack(&s,0); s.battle.my_ki=0;
    check("sway_posture_needs_ki",rejection(&s,UDE_DB_SONIC_SWAY,UDE_R_NO_KI));
    s=base(); s.battle.recipe[UDE_X_FIRST_HIT]=0;
    check("GUARD_HOLD",selected(&s,UDE_DB_GUARD_HOLD));
    s.battle.behind=1; check("guard_from_behind",rejection(&s,UDE_DB_GUARD_HOLD,UDE_R_BEHIND));
    for(i=11;i<163;i++) {
        const UdeDbAttack *a=UdeDb_Attack(0,i);
        if(a->guard[0]==3 && a->guard[1]==0 && a->guard[2]!=4) {high=i;break;}
    }
    check("high_guard_fixture_exists",high>=0);
    s=base(); attack(&s,high); s.battle.recipe[UDE_X_FIRST_HIT]=0;
    check("GUARD_HIGH_LOW",selected(&s,UDE_DB_GUARD_HIGH_LOW)&&(UdeDb_Choose(&s).added&0x10));
    s.battle.my_action=0x38; check("guard_pose_three_frame_limit",rejection(&s,UDE_DB_GUARD_HIGH_LOW,UDE_DB_R_POSE_TOO_LATE));
    s.battle.recipe[UDE_X_UNTIL100]=300; check("guard_pose_can_prepare",selected(&s,UDE_DB_GUARD_HIGH_LOW));
    s=base(); attack(&s,0x55); s.battle.opp_action=0xB4; s.battle.recipe[UDE_X_GRABBING]=1;
    check("THROW_BREAK",selected(&s,UDE_DB_THROW_BREAK));
    s.battle.behind=1; check("throw_behind",rejection(&s,UDE_DB_THROW_BREAK,UDE_R_BEHIND));
    s.battle.behind=0; s.battle.anim_flags|=0x800; check("throw_halved_damage_animation",rejection(&s,UDE_DB_THROW_BREAK,UDE_DB_R_ANIMATION));
    s=base(); s.battle.opp_action=0x11B; s.battle.recipe[UDE_X_RUSHING]=1;
    check("RUSH_EVADE",selected(&s,UDE_DB_RUSH_EVADE));
    s.battle.recipe[UDE_X_OPP_A5]=1; s.battle.recipe[UDE_X_GRABBING]=1;
    check("rush_grab_uses_throw_recipe",selected(&s,UDE_DB_THROW_BREAK));
    s.battle.win[5]=-29; check("rush_grab_uses_107C",rejection(&s,UDE_DB_THROW_BREAK,UDE_R_WINDOW_COOLING));
    s=projectile(1); check("BLAST_EVADE",selected(&s,UDE_DB_BLAST_EVADE));
    s.projectile_hits=1; check("beam_later_hit_guard",selected(&s,UDE_DB_GUARD_HOLD)&&rejection(&s,UDE_DB_BLAST_EVADE,UDE_DB_R_LATER_HIT));
    s.projectile_hits=-1; check("beam_count_unknown_guard",selected(&s,UDE_DB_GUARD_HOLD)&&rejection(&s,UDE_DB_BLAST_EVADE,UDE_DB_R_FIRST_HIT_UNKNOWN));
    s=projectile(0); check("KI_DEFLECT",selected(&s,UDE_DB_KI_DEFLECT));
    s.previous_added=0x100041;s.battle.my_action=0x38;
    check("depleted_deflect_stance_renews_its_edge",UdeDb_Choose(&s).reason==UDE_DB_R_RELEASE);
    s.battle.my_action=0x3B;check("active_deflect_stance_stays_held",selected(&s,UDE_DB_KI_DEFLECT));
    s.previous_added=0;s.battle.my_action=0xB;
    s.own_flag41=1; check("PERFECT_DEFLECT",selected(&s,UDE_DB_PERFECT_DEFLECT));
    s.own_flag41=-1; check("perfect_flag_not_inferred",rejection(&s,UDE_DB_PERFECT_DEFLECT,UDE_DB_R_CAPABILITY));
    s=projectile(0); s.battle.recipe[UDE_X_INC]|=UDE_INC_BEHIND;
    check("deflect_behind",rejection(&s,UDE_DB_KI_DEFLECT,UDE_R_BEHIND));
    s=projectile(0); s.battle.recipe[UDE_X_INC]&=~UDE_INC_REDIRECT;
    check("unredirectable_blast_guard",selected(&s,UDE_DB_GUARD_HOLD));
    s=projectile(0); s.battle.recipe[UDE_X_CAPS]=0;
    check("no_deflect_capability_guard",selected(&s,UDE_DB_GUARD_HOLD));
    s=projectile(0); s.battle.recipe[UDE_X_CAPS]=0; s.battle.close=1; s.config.sidestep=1;
    check("SIDESTEP_opt_in",selected(&s,UDE_DB_SIDESTEP));
    s=base(); s.battle.my_action=0xD5; s.battle.my_action_frame=9; s.battle.recover_ok=1; s.config.recover=1;
    check("RECOVERY_opt_in",selected(&s,UDE_DB_RECOVERY));
    s.config.recover=0; check("recovery_off_by_default",rejection(&s,UDE_DB_RECOVERY,UDE_R_DISABLED));
    for(kind=1;kind<UDE_DB_RECIPE_COUNT;kind++) {
        int forbidden=kind==UDE_DB_VANISHING_STEP||kind==UDE_DB_VANISH_BEHIND||kind==UDE_DB_REFLECT_ABSORB||kind==UDE_DB_DRAGON_DASH_ESCAPE||
            kind==UDE_DB_EMERGENCY_BLASTER_WAVE||kind==UDE_DB_ARMOR_REPEL||kind==UDE_DB_SKILL_DEFENSE||kind==UDE_DB_CLASH_MASH||kind==UDE_DB_CLASH_PROMPT;
        if(forbidden) {
            s=base();s.config.kinds=~0u;s.config.dragon_escape=1;
            check(UdeDb_RecipeName(kind),rejection(&s,kind,UDE_DB_R_FORBIDDEN)&&UdeDb_Choose(&s).recipe!=kind);
        }
    }
    s=base();s.battle.opp_action=0xFA;check("clash_never_overridden_by_stale_attack",UdeDb_Choose(&s).reason==UDE_DB_R_FORBIDDEN);
    s=base();s.battle.opp_action=0xFD;check("skill_never_automatic",UdeDb_Choose(&s).reason==UDE_DB_R_FORBIDDEN);
    s=base();s.battle.my_action=0xEC;check("own_transform_never_interrupted",UdeDb_Choose(&s).reason==UDE_DB_R_FORBIDDEN);
    cfg=s.config;cfg.enabled=1;s=base();UdeDb_Reset(&state);
    UdeDb_Step(&state,&cfg,&s,0,10,&d);check("tap_starts",d.press_edge&&d.recipe==UDE_DB_VANISH_EVADE);
    UdeDb_Step(&state,&cfg,&s,0,11,&d);check("next_update_release",d.added==0&&d.reason==UDE_DB_R_RELEASE);
    for(i=12;i<40;i++) {UdeDb_Step(&state,&cfg,&s,0,(unsigned)i,&d);if(d.press_edge)errors++;}
    check("never_two_presses_within_30_updates",errors==0);
    UdeDb_Step(&state,&cfg,&s,0,40,&d);check("30_update_boundary",d.press_edge);
    UdeDb_Step(&state,&cfg,&s,0x80,41,&d);check("manual_on_release_frame_preserved",d.buttons==0x80&&d.added==0);
    UdeDb_Reset(&state);s=base();UdeDb_Step(&state,&cfg,&s,8,1,&d);
    s.battle.my_action=0x44;
    for(i=2;i<12;i++)UdeDb_Step(&state,&cfg,&s,0,(unsigned)i,&d);
    check("manual_action_survives_grace",d.added==0&&d.reason==UDE_R_MANUAL_ACTION);
    s.battle.my_action=0xB;UdeDb_Step(&state,&cfg,&s,0,12,&d);check("claim_ends_at_neutral",d.recipe==UDE_DB_VANISH_EVADE);
    UdeDb_Reset(&state);s.battle.my_action=0x44;
    for(i=0;i<20;i++)UdeDb_Step(&state,&cfg,&s,0,(unsigned)i,&d);
    check("enabling_mid_manual_action_does_not_take_ownership",d.added==0&&d.reason==UDE_R_MANUAL_ACTION);
    s.battle.my_action=0xB;
    UdeDb_Step(&state,&cfg,&s,0,200,&d);check("gap_cannot_fabricate_fresh_edge",d.added==0);
    UdeDb_Reset(&state);errors=0;last_press=-100;s=base();
    for(i=0;i<20000;i++) {
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        physical=(rng&7)==0?(rng&0x7fffu):0;
        s.battle.online=(rng&127)==0; s.battle.my_request_pending=(rng&63)==0;
        s.battle.win[0]=(rng&15)==0?-29:-30;
        out=UdeDb_Step(&state,&cfg,&s,physical,(unsigned)i,&d);
        if((out&physical)!=physical||(physical&&d.added))errors++;
        if((s.battle.online||s.battle.my_request_pending)&&d.added)errors++;
        if(d.press_edge){if(i-last_press<30)errors++;last_press=i;}
        if(d.added&~(0x1000001u|0x100000u|0xfu|0xf0u))errors++;
    }
    check("20000_update_physical_safety_and_press_spacing",errors==0&&state.manual_overwrites==0);
    printf("TOTAL,%d,FAILED,%d\n",total,failed);
    return failed?1:0;
}
