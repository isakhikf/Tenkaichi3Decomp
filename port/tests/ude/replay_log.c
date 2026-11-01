/*
 * Replay of a REAL ude_log.csv through the host engine (cycle 5).
 *   mode fidelity:  the logged snapshot + the logged physical buttons, frame by frame. Every row whose previous row is the
 *                   previous frame is compared with what the game logged (reason, injected, threat). Rows after a gap
 *                   are re-synchronised (the engine's counters during unlogged frames are unknown) and not compared.
 *   mode handsoff:  the same snapshots with no physical input (counterfactual: "what would the engine have sent if the
 *                   player had let go"). Output per frame for tools/validation/replay_report.py.
 * LEVEL: host replay of real snapshots. It says what the decision code does with the game's real inputs; it does NOT
 * say what the game would have done with the guard (no collision is simulated).
 * Usage: replay_log <ude_log.csv> fidelity|handsoff
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ude.h"

#define MAXC 64
static char *col[MAXC];
static int ncol;
static int idx(const char *name, char **hdr, int n) { int i; for (i = 0; i < n; i++) if (!strcmp(hdr[i], name)) return i; return -1; }
static int split(char *line) { char *p = line; ncol = 0; while (ncol < MAXC) { col[ncol++] = p; p = strchr(p, ','); if (!p) break; *p++ = 0; } return ncol; }
static int threat_id(const char *s) { int t; for (t = 0; t < UDE_T_COUNT; t++) if (!strcmp(Ude_ThreatName(t), s)) return t; return -1; }

int main(int argc, char **argv) {
    FILE *f; char line[4096], hdrline[4096]; char *hdr[MAXC]; int nh, i;
    int I[40]; const char *names[] = { "frame", "player", "my_action", "opp_action", "threat", "injected", "reason", "close", "blast",
        "opp_attacking", "skill_kind", "hp", "ki", "buttons", "behind", "anim_flags", "attack_id", "attack_flags", "hit_active", "flag5",
        "request_pending", "opp_action_frame", "blast_eta10", "w_evade", "w_blast", "w_counter", "w_perfect", "w_throw", "w_rush" };
    int handsoff; UdeState st[2]; UdeConfig c; long rows = 0, compared = 0, same_reason = 0, same_inj = 0, same_threat = 0, prevf[2] = { -9, -9 };
    if (argc < 3 || !(f = fopen(argv[1], "r"))) { fprintf(stderr, "usage: replay_log <ude_log.csv> fidelity|handsoff\n"); return 2; }
    handsoff = !strcmp(argv[2], "handsoff");
    if (!fgets(hdrline, sizeof hdrline, f)) return 2;
    hdrline[strcspn(hdrline, "\r\n")] = 0;
    { char *p = hdrline; nh = 0; while (nh < MAXC) { hdr[nh++] = p; p = strchr(p, ','); if (!p) break; *p++ = 0; } }
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) if ((I[i] = idx(names[i], hdr, nh)) < 0) { fprintf(stderr, "missing column %s\n", names[i]); return 3; }
    Ude_DefaultConfig(&c); Ude_Reset(&st[0]); Ude_Reset(&st[1]);
    if (handsoff) printf("frame,player,threat,injected,reason,response\n");
    while (fgets(line, sizeof line, f)) {
        UdeSnapshot s; UdeDecision d; int p, fr; unsigned phys;
        line[strcspn(line, "\r\n")] = 0;
        if (split(line) < nh) continue;
        fr = atoi(col[I[0]]); p = atoi(col[I[1]]) & 1;
        memset(&s, 0, sizeof s);
        s.valid = 1; s.fighting = 1; s.my_action = atoi(col[I[2]]); s.opp_action = atoi(col[I[3]]); s.close = atoi(col[I[7]]);
        s.incoming_blast = atoi(col[I[8]]); s.opp_attacking = atoi(col[I[9]]); s.opp_skill_kind = atoi(col[I[10]]);
        s.my_hp = atoi(col[I[11]]); s.my_ki = atoi(col[I[12]]); s.behind = atoi(col[I[14]]); s.have_ext = 1;
        s.anim_flags = (int)strtol(col[I[15]], NULL, 0); s.opp_attack_id = atoi(col[I[16]]); s.opp_attack_flags = (int)strtol(col[I[17]], NULL, 0);
        s.opp_hit_active = atoi(col[I[18]]); s.flag5 = atoi(col[I[19]]); s.my_request_pending = atoi(col[I[20]]);
        s.opp_action_frame = atoi(col[I[21]]); s.blast_eta10 = atoi(col[I[22]]);
        for (i = 0; i < UDE_W_COUNT; i++) s.win[i] = atoi(col[I[23 + i]]);
        phys = (unsigned)strtoul(col[I[13]], NULL, 0);
        if (handsoff) phys &= ~(0xFFFFFFFFu & ~0u);            /* no physical input at all */
        if (!handsoff && fr != prevf[p] + 1) {                 /* gap: resynchronise the counters the log cannot show */
            Ude_Reset(&st[p]);
            if (!strcmp(col[I[6]], "manual") && !(phys & ~(UDE_B_DIR_MASK | UDE_B_DERIVED))) st[p].last_manual = 0;
        }
        Ude_Step(&st[p], &c, &s, phys, &d);
        if (handsoff) {
            printf("%d,%d,%s,%d,%s,%s\n", fr, p, Ude_ThreatName(d.threat), d.injected, Ude_ReasonName(d.reason), Ude_ResponseName(d.response));
        } else if (fr == prevf[p] + 1) {
            compared++;
            same_reason += !strcmp(Ude_ReasonName(d.reason), col[I[6]]);
            if (getenv("UDE_REPLAY_DIFF") && strcmp(Ude_ReasonName(d.reason), col[I[6]])) fprintf(stderr, "diff frame %d: host %s game %s my=0x%X buttons=%s\n", fr, Ude_ReasonName(d.reason), col[I[6]], s.my_action, col[I[13]]);
            same_inj += d.injected == atoi(col[I[5]]);
            same_threat += d.threat == threat_id(col[I[4]]);
        }
        prevf[p] = fr; rows++;
    }
    fclose(f);
    if (!handsoff) printf("rows=%ld compared=%ld same_reason=%ld same_injected=%ld same_threat=%ld\n", rows, compared, same_reason, same_inj, same_threat);
    return 0;
}
