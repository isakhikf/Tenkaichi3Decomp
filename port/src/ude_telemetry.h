/*
 * UDE offline telemetry (ude_log=1). Pure C, no game headers, no allocation: two fixed 64 KB line buffers, written
 * to disk in batches (when 3/4 full, or when the fight is not running), never per line.
 *
 * Files (next to the game):
 *   ude_log.csv     per-frame decision lines, the cycle-2 format (import_log.py reads it as before)
 *   ude_events.csv  structured events, format 3: SESSION, CONTACT, PRESS, CONFLICT, SUMMARY (import_log.py v3)
 *
 * Contact timing comes from the engine, not from clocks: the collision results of a frame (reaction id, guard /
 * evasion flags) are read at the next input sample, together with the opponent's action counter (+0x964), which the
 * action update advanced BEFORE that collision step. So `opp_frame` of a CONTACT is the counter value at contact,
 * on the same scale the engine reads when it decides (src/battle/btl_char_mgr.c frame order).
 * Nothing personal, no network data, no game data beyond ids and counters.
 */
#ifndef UDE_TELEMETRY_H
#define UDE_TELEMETRY_H
#include <stdio.h>
#include "ude.h"

#define UDE_TEL_FORMAT 3
#define UDE_TEL_BUF 65536

typedef struct UdeTelIn {          /* per-frame values beyond the engine snapshot (all scalars) */
    int sim_frame;                 /* BtlChar_GetFrame(): the simulation's frame counter */
    int player, my_chara, opp_chara;
    int my_motion, opp_motion;
    int distance10, opp_yaw1000, altitude10;
    int reaction;                  /* own reaction id after the last hit step, 1 = none */
    int evade_flags, guard_flags;  /* BtlHit_PortUdeQuery 17 / 18 */
    int opp_broken;                /* opponent flag 0x7A */
} UdeTelIn;

typedef struct UdeLineBuf {
    FILE *f;
    int len;
    unsigned long long written, dropped, lines;
    char data[UDE_TEL_BUF];
} UdeLineBuf;

typedef struct UdeTelemetry {
    UdeLineBuf frames, events;
    int session, open, idle, release;
    int log_all;                   /* ude_log=2: a per-frame line for EVERY fight frame (diagnosis of unlogged contacts) */
    int prev_hp, prev_injected, prev_my_action, prev_reason, prev_response;   /* the decision of the contact frame */
    int press_pending, press_frame, press_resp, press_threat;   /* last press edge, for its acceptance check */
    int last_press_frame, last_press_resp;
    unsigned contacts, presses, accepted, conflicts, threats;
    unsigned perf_bins[16];        /* decision time histogram, bin k: [2^k, 2^(k+1)) ns */
    unsigned long long perf_n, perf_sum, perf_max;
} UdeTelemetry;

/* `dir` "" = current directory. release = PORT_RELEASE. Returns 0 when the files cannot be opened (telemetry off). */
int UdeTel_Open(UdeTelemetry *t, const char *dir, int release);
/* One call per engine step. decide_ns = time spent in snapshot + Ude_Step (0 when not measured). */
void UdeTel_Frame(UdeTelemetry *t, const UdeSnapshot *s, const UdeTelIn *in, const UdeDecision *d, unsigned physical,
                  unsigned long long decide_ns);
/* Writes the buffers out (called when the fight is not running and at exit). */
void UdeTel_Flush(UdeTelemetry *t);
void UdeTel_Close(UdeTelemetry *t);

#endif
