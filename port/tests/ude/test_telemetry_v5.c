/*
 * Hybrid v5 telemetry checks (LEVEL: unit on the host; no game). The real engine (ude.c + ude_recipe.c) and the real
 * telemetry (ude_telemetry.c) on short scripted moments; the CONTACT lines of ude_events.csv are then read back.
 *  - a recipe deflect followed by the native deflect outcome (query 25 bit 1) is a CONTACT "deflected", credited to
 *    the recipe (before v5 a deflect left no reaction, no damage and no guard flag: no CONTACT at all);
 *  - a counter outcome (query 25 bit 8) is "countered";
 *  - a defence the player pressed himself is credited to "manual", never to the engine.
 * Usage: test_telemetry_v5 <out_dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ude.h"
#include "ude_telemetry.h"

static int fails, total;
static void emit(const char *n, int ok, const char *d) {
    printf("v5,logic_sim,%s,0,-,-,-,0,%s,0,-,%s,-\n", n, ok ? "PASS" : "FAIL", d);
    fails += !ok;
    total++;
}

static UdeTelemetry T;

static void snap(UdeSnapshot *s, int eta100) {
    int k;
    memset(s, 0, sizeof *s);
    s->valid = s->fighting = s->have_ext = s->have_recipe = 1;
    s->my_action = 0x38; s->opp_action = 0xAE; s->opp_skill_kind = -1; s->my_ki = 50000; s->my_hp = 30000; s->flag5 = 1;
    s->opp_attack_id = -1; s->blast_eta10 = eta100 / 10; s->incoming_blast = eta100 >= 0; s->game_free = 1;
    for (k = 0; k < UDE_W_COUNT; k++) s->win[k] = -30;
    s->recipe[UDE_X_OK] = 1; s->recipe[UDE_X_FREE] = 1; s->recipe[UDE_X_FLAG5] = 1; s->recipe[UDE_X_MY_ACT] = 0x38;
    s->recipe[UDE_X_OPP_ACT] = 0xAE; s->recipe[UDE_X_OWN_FLAGS] = 0xB1; s->recipe[UDE_X_MY_KI] = 50000; s->recipe[UDE_X_CAPS] = 0x20;
    s->recipe[UDE_X_W1068] = s->recipe[UDE_X_W106C] = s->recipe[UDE_X_W1070] = s->recipe[UDE_X_W1078] = s->recipe[UDE_X_W107C] = -30;
    s->recipe[UDE_X_W1074] = -15;
    s->recipe[UDE_X_UNTIL100] = s->recipe[UDE_X_UNTIL_FLOOR100] = UDE_X_NONE100;
    s->recipe[UDE_X_CAST_ETA100] = -1; s->recipe[UDE_X_STUN_REACT] = s->recipe[UDE_X_STUN_DIR] = -1; s->recipe[UDE_X_ATK] = -1;
    s->recipe[UDE_X_SLOT] = s->recipe[UDE_X_RUSHTYPE] = -1; s->recipe[UDE_X_FIRST_EVENT] = -1; s->recipe[UDE_X_OPP_ANIM] = 0x80;
    if (eta100 >= 0) {
        s->recipe[UDE_X_INC] = s->recipe[UDE_X_VOLLEY] = s->recipe[UDE_X_PENDING24] = UDE_INC_HIT | UDE_INC_REDIRECT | UDE_INC_ABSORB;
        s->recipe[UDE_X_INC_ETA100] = eta100; s->recipe[UDE_X_PROJ_FIRST] = 1;
    } else {
        s->recipe[UDE_X_INC_ETA100] = UDE_X_NONE100;
    }
}

/* one update: engine + telemetry; `outcome`, `guard` are the results of the PREVIOUS update's collision step */
static void frame(UdeState *st, UdeConfig *c, int f, int eta100, unsigned phys, int outcome, int guard, int hp) {
    UdeSnapshot s;
    UdeTelIn in;
    UdeDecision d;
    snap(&s, eta100);
    s.my_hp = hp;
    memset(&in, 0, sizeof in);
    in.sim_frame = f; in.player = 0; in.my_chara = 21; in.opp_chara = 0; in.reaction = 1; in.distance10 = 300;
    in.outcome = outcome; in.guard_flags = guard;
    Ude_Step(st, c, &s, phys, &d);
    UdeTel_Frame(&T, &s, &in, &d, phys, 1000);
}

int main(int argc, char **argv) {
    UdeConfig c;
    UdeState st;
    char path[512], line[1024], det[256];
    FILE *f;
    int i, fr = 100, deflected = 0, deflect_credit = 0, countered = 0, manual = 0, manual_credit_engine = 0;

    if (argc < 2 || !UdeTel_Open(&T, argv[1], 139)) { fprintf(stderr, "usage: test_telemetry_v5 <out_dir>\n"); return 3; }
    Ude_DefaultConfig(&c);
    c.recipes = 1;
    c.manual_claim = 1;
    Ude_Reset(&st);

    /* 1. a frontal ki blast: the recipe presses GUARD + side, the game deflects it on the contact update */
    for (i = 0; i < 10; i++) frame(&st, &c, fr++, (8 - i) * 100, 0, 0, 0, 30000);
    frame(&st, &c, fr++, -1, 0, 1, 0, 30000);           /* deflect outcome seen at the next sample */
    for (i = 0; i < 40; i++) frame(&st, &c, fr++, -1, 0, 0, 0, 30000);
    /* 2. a countered strike (opponent flag 0x6F) */
    frame(&st, &c, fr++, -1, 0, 8, 0, 30000);
    for (i = 0; i < 40; i++) frame(&st, &c, fr++, -1, 0, 0, 0, 30000);
    /* 3. the player guards himself, then the blast is guarded */
    for (i = 0; i < 6; i++) frame(&st, &c, fr++, (6 - i) * 100, i >= 2 ? 0x1u : 0, 0, 0, 30000);
    frame(&st, &c, fr++, -1, 0x1u, 0, 1, 30000);
    for (i = 0; i < 40; i++) frame(&st, &c, fr++, -1, 0, 0, 0, 30000);
    UdeTel_Close(&T);

    snprintf(path, sizeof path, "%s/ude_events.csv", argv[1]);
    f = fopen(path, "r");
    if (!f) { emit("v5_tel_events_file", 0, "no ude_events.csv"); return 1; }
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "CONTACT,", 8) != 0) continue;
        if (strstr(line, ",deflected,")) { deflected++; if (strstr(line, ",deflect,")) deflect_credit++; }
        if (strstr(line, ",countered,")) countered++;
        if (strstr(line, ",manual,")) manual++;
        if (strstr(line, ",guarded,") && !strstr(line, ",manual,")) manual_credit_engine++;
    }
    fclose(f);
    snprintf(det, sizeof det, "deflected contacts %d (credited to the deflect recipe %d)", deflected, deflect_credit);
    emit("v5_tel_deflect_visible", deflected == 1 && deflect_credit == 1, det);
    snprintf(det, sizeof det, "countered contacts %d", countered);
    emit("v5_tel_counter_visible", countered == 1, det);
    snprintf(det, sizeof det, "player's own guard: manual %d, credited to the engine %d", manual, manual_credit_engine);
    emit("v5_tel_manual_not_credited", manual == 1 && manual_credit_engine == 0, det);
    fprintf(stderr, "v5 telemetry: %d tests, %d failed\n", total, fails);
    return fails;
}
