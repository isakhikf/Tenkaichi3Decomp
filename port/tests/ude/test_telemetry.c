/*
 * Telemetry driver (LEVEL: logic simulation on the host). Feeds the real engine (ude.c) and the real telemetry
 * (ude_telemetry.c) with a modelled fight whose ground truth is known, and writes ude_log.csv / ude_events.csv into
 * the directory given. tests/test_pipeline.py imports them and checks the database against tests/data/telemetry_truth.txt.
 *
 * The model follows the game's order: input sample (engine + telemetry) -> action update (opponent counter +1) -> hit
 * step; the hit step's results (reaction, guard / evasion flags) are visible at the NEXT sample. opp_frame recorded
 * for a contact must equal the counter during the hit step.
 * Usage: test_telemetry <out_dir> <variant: 0 consistent, 1 contradictory, 2 stress, 3 = 0 with the assist off>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ude.h"
#include "ude_telemetry.h"

static UdeTelemetry T;
static FILE *truth;

typedef struct Atk { int action, contact, chara, behind; unsigned manual_at; } Atk;

static void fight(UdeConfig *c, const Atk *atk, int n, int my_chara, int *frame) {
    UdeState st; int k, hp = 30000, i;
    int pend_reaction = 1, pend_guard = 0, pend_evade = 0, my = 0x0B, w0 = -30;
    Ude_Reset(&st);
    for (k = 0; k < n; k++) {
        int counter = 0, total = atk[k].contact + 8;
        for (i = 0; i < total; i++) {
            UdeSnapshot s; UdeTelIn in; UdeDecision d; unsigned phys = 0; int contact_now, on;
            int action = i < atk[k].contact + 2 ? atk[k].action : 0x0B;
            memset(&s, 0, sizeof s); memset(&in, 0, sizeof in);
            s.valid = s.fighting = 1; s.close = 1; s.behind = atk[k].behind; s.my_action = my; s.opp_action = action;
            s.opp_attacking = action != 0x0B; s.opp_skill_kind = -1; s.my_ki = 50000; s.my_hp = hp; s.have_ext = 1; s.flag5 = 1;
            s.anim_flags = 0xB1; s.opp_attack_id = 3; s.opp_attack_flags = 1; s.opp_action_frame = counter; s.blast_eta10 = -1;
            { int w; for (w = 0; w < UDE_W_COUNT; w++) s.win[w] = -30; } s.win[0] = w0;
            if (atk[k].manual_at && i == (int)atk[k].manual_at) phys = 0x8;     /* the player presses RUSH */
            in.sim_frame = *frame; in.player = 0; in.my_chara = my_chara; in.opp_chara = atk[k].chara;
            in.reaction = pend_reaction; in.guard_flags = pend_guard; in.evade_flags = pend_evade; in.distance10 = 400;
            in.altitude10 = 0; in.opp_yaw1000 = 0;
            Ude_Step(&st, c, &s, phys, &d);
            UdeTel_Frame(&T, &s, &in, &d, phys, 900 + (unsigned)(i % 7) * 100);
            /* the game, this frame: action update, timers, hit step */
            if (action != 0x0B) counter++; else counter = 0;
            on = d.press_edge;
            w0--; if (w0 < -30) { w0 = -30; if (on) w0 = 3; } else if (on) w0 = -1;
            pend_reaction = 1; pend_guard = 0; pend_evade = 0;
            contact_now = action != 0x0B && counter == atk[k].contact;
            if (contact_now) {
                if (w0 > 0) { pend_evade = 1; my = 0x21; }
                else if (my >= 0x36 && my <= 0x46 && !atk[k].behind) { pend_guard = 1; my = 0x38; }
                else { pend_reaction = 5; hp -= 500; my = 0xB9; }
                fprintf(truth, "contact 0x%X %d %d\n", atk[k].action, atk[k].chara, atk[k].contact);
            } else if (my == 0xB9 || my == 0x21 || my == 0x38) {
                my = 0x0B;
            } else {
                my = (d.buttons & UDE_B_GUARD) ? 0x36 : 0x0B;
            }
            (*frame)++;
        }
    }
    /* the fight ends: 40 frames outside the fight state */
    for (i = 0; i < 40; i++) {
        UdeSnapshot s; UdeTelIn in; UdeDecision d;
        memset(&s, 0, sizeof s); memset(&in, 0, sizeof in); s.valid = 1; s.fighting = 0; s.opp_action = s.my_action = 0x0B;
        in.sim_frame = *frame; in.reaction = 1;
        Ude_Step(&st, c, &s, 0, &d); UdeTel_Frame(&T, &s, &in, &d, 0, 0); (*frame)++;
    }
}

int main(int argc, char **argv) {
    UdeConfig c; int frame = 1000; char p[512];
    int variant = argc > 2 ? atoi(argv[2]) : 0;
    if (argc < 2) return 2;
    snprintf(p, sizeof p, "%s/telemetry_truth.txt", argv[1]);
    truth = fopen(p, variant ? "a" : "w");
    if (!UdeTel_Open(&T, argv[1], 139)) return 3;
    Ude_DefaultConfig(&c);
    Ude_ClearEta();
    if (variant == 3) c.enabled = 0;   /* campaign A: the same fight, assist off */
    if (!variant || variant == 3) {
        /* session 1: character 12 attacks, front (guard) and behind (no timing: no input) */
        Atk s1[] = { {0x70, 7, 12, 0, 0}, {0x70, 7, 12, 0, 0}, {0x70, 7, 12, 1, 0}, {0x71, 9, 12, 0, 0}, {0x71, 9, 12, 0, 0}, {0x71, 9, 12, 0, 0} };
        /* session 2: character 30, same action 0x71 with another timing (per-character variation); a manual conflict */
        Atk s2[] = { {0x71, 11, 30, 0, 0}, {0x71, 11, 30, 0, 0}, {0x71, 11, 30, 0, 6}, {0x70, 7, 30, 0, 0} };
        fight(&c, s1, 6, 3, &frame);
        fight(&c, s2, 4, 3, &frame);
        /* session 3: timing known now (as if ude_eta.txt were loaded): behind attack evaded by a tap */
        Ude_SetEta(0x70, 7);
        { Atk s3[] = { {0x70, 7, 12, 1, 0}, {0x70, 7, 12, 1, 0} }; fight(&c, s3, 2, 3, &frame); }
    } else if (variant == 2) {
        /* stress: 400 attacks in one fight, about 6000 logged frames (> 64 KB): the buffer flushes mid-fight */
        static Atk many[400]; int k;
        for (k = 0; k < 400; k++) { many[k].action = 0x70 + k % 8; many[k].contact = 7; many[k].chara = 12; many[k].behind = 0; many[k].manual_at = 0; }
        fight(&c, many, 400, 3, &frame);
    } else {
        /* a second log with contradictory timings for 0x72 / character 12 */
        Atk s4[] = { {0x72, 5, 12, 0, 0}, {0x72, 12, 12, 0, 0}, {0x72, 5, 12, 0, 0} };
        frame = 50000;
        fight(&c, s4, 3, 3, &frame);
    }
    UdeTel_Close(&T);
    fclose(truth);
    printf("frames=%d lines_frames=%llu bytes, events=%llu bytes, dropped=%llu frame_lines=%llu event_lines=%llu\n", frame,
           T.frames.written, T.events.written, T.frames.dropped + T.events.dropped, T.frames.lines, T.events.lines);
    return 0;
}
