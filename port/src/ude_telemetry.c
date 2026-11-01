/* UDE offline telemetry: see ude_telemetry.h. */
#include <stdarg.h>
#include <string.h>
#include "ude_telemetry.h"

static void buf_flush(UdeLineBuf *b) {
    if (b->f != NULL && b->len > 0) {
        fwrite(b->data, 1, (size_t)b->len, b->f);
        fflush(b->f);
        b->written += (unsigned long long)b->len;
    }
    b->len = 0;
}

static void buf_line(UdeLineBuf *b, const char *fmt, ...) {
    va_list ap;
    int n;
    if (b->f == NULL) return;
    if (b->len > UDE_TEL_BUF * 3 / 4) buf_flush(b);         /* batch write; rare inside a fight (about 1 per 300 lines) */
    va_start(ap, fmt);
    n = vsnprintf(b->data + b->len, (size_t)(UDE_TEL_BUF - b->len), fmt, ap);
    va_end(ap);
    if (n < 0 || n >= UDE_TEL_BUF - b->len) { b->dropped++; return; }   /* a line never splits: dropped and counted */
    b->len += n;
    b->lines++;
}

static FILE *open_with_header(const char *dir, const char *name, const char *header) {
    char path[512];
    FILE *f;
    long size;
    snprintf(path, sizeof path, "%s%s%s", dir, (dir[0] && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != '\\') ? "/" : "", name);
    f = fopen(path, "a");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size <= 0) fputs(header, f);
    return f;
}

int UdeTel_Open(UdeTelemetry *t, const char *dir, int release) {
    memset(t, 0, sizeof *t);
    t->release = release;
    t->frames.f = open_with_header(dir, "ude_log.csv",
        "frame,player,my_action,my_motion,opp_action,opp_motion,threat,injected,reason,close,blast,opp_attacking,skill_kind,hp,ki,buttons,"
        "response,confidence,press_edge,behind,anim_flags,attack_id,attack_flags,hit_active,flag5,request_pending,opp_action_frame,blast_eta10,"
        "w_evade,w_blast,w_counter,w_perfect,w_throw,w_rush,recover_ok,game_free\n");
    t->events.f = open_with_header(dir, "ude_events.csv",
        "type,session,frame,player,my_chara,opp_chara,opp_action,opp_frame,my_action,attack_id,distance10,behind,opp_yaw1000,altitude10,"
        "reaction,evade_flags,guard_flags,opp_broken,hp_delta,defense,press_age,accepted,result,detail\n");
    t->prev_hp = -1;
    t->last_press_frame = -100000;
    return t->frames.f != NULL && t->events.f != NULL;
}

static const char *result_of(const UdeTelIn *in, int hp_delta) {
    if (in->evade_flags & 0x1F) return "evaded";
    if (in->guard_flags & 32) return "broken";
    if (in->guard_flags & 64) return "guard_crushed";
    if (in->guard_flags & (1 | 2 | 4 | 8 | 16)) return hp_delta < 0 ? "guarded_chip" : "guarded";
    if (hp_delta < 0 || in->reaction != 1) return "hit";
    return "none";
}

static void event(UdeTelemetry *t, const char *type, const UdeSnapshot *s, const UdeTelIn *in, int hp_delta,
                  const char *defense, int press_age, int accepted, const char *result, const char *detail) {
    buf_line(&t->events, "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,%s,%s\n", type, t->session, in->sim_frame,
             in->player, in->my_chara, in->opp_chara, s->opp_action, s->opp_action_frame, s->my_action, s->opp_attack_id,
             in->distance10, s->behind, in->opp_yaw1000, in->altitude10, in->reaction, in->evade_flags, in->guard_flags,
             in->opp_broken, hp_delta, defense, press_age, accepted, result, detail);
}

static void summary(UdeTelemetry *t, const UdeSnapshot *s, const UdeTelIn *in) {
    char d[256];
    int k, n = 0;
    unsigned long long acc = 0, p50 = 0, p99 = 0;
    for (k = 0; k < 16; k++) {
        acc += t->perf_bins[k];
        if (!p50 && t->perf_n && acc * 2 >= t->perf_n) p50 = 1ull << (k + 1);
        if (!p99 && t->perf_n && acc * 100 >= t->perf_n * 99) p99 = 1ull << (k + 1);
    }
    n = snprintf(d, sizeof d, "contacts=%u presses=%u accepted=%u conflicts=%u threat_frames=%u decide_n=%llu decide_avg_ns=%llu "
                 "decide_p50_ns<=%llu decide_p99_ns<=%llu decide_max_ns=%llu dropped_lines=%llu",
                 t->contacts, t->presses, t->accepted, t->conflicts, t->threats, t->perf_n, t->perf_n ? t->perf_sum / t->perf_n : 0,
                 p50, p99, t->perf_max, t->frames.dropped + t->events.dropped);
    (void)n;
    event(t, "SUMMARY", s, in, 0, "-", -1, -1, "-", d);
}

void UdeTel_Frame(UdeTelemetry *t, const UdeSnapshot *s, const UdeTelIn *in, const UdeDecision *d, unsigned physical,
                  unsigned long long ns) {
    int hp_delta, manual = (physical & ~(UDE_B_DIR_MASK | UDE_B_DERIVED)) != 0;

    if (!s->fighting) {                  /* between fights: close the session after a second, write out */
        if (t->open && ++t->idle >= 30) {
            summary(t, s, in);
            t->open = 0;
            UdeTel_Flush(t);
        }
        return;
    }
    t->idle = 0;
    if (!t->open) {
        char det[96];
        t->open = 1;
        t->session++;
        t->prev_hp = s->my_hp;
        t->contacts = t->presses = t->accepted = t->conflicts = t->threats = 0;
        memset(t->perf_bins, 0, sizeof t->perf_bins);
        t->perf_n = t->perf_sum = t->perf_max = 0;
        snprintf(det, sizeof det, "format=%d release=%d ude=3 ext=%d", UDE_TEL_FORMAT, t->release, s->have_ext);
        event(t, "SESSION", s, in, 0, "-", -1, -1, "-", det);
    }
    if (ns) {
        int k = 0;
        unsigned long long v = ns;
        while (v > 1 && k < 15) { v >>= 1; k++; }
        t->perf_bins[k]++;
        t->perf_n++;
        t->perf_sum += ns;
        if (ns > t->perf_max) t->perf_max = ns;
    }
    if (d->threat) t->threats++;
    hp_delta = (t->prev_hp >= 0) ? s->my_hp - t->prev_hp : 0;

    /* acceptance of the previous frame's press: a window above 0 or a guard action now */
    if (t->press_pending) {
        int acc = s->win[UDE_W_EVADE] > 0 || s->win[UDE_W_BLAST] > 0 || s->win[UDE_W_THROW] > 0 || s->win[UDE_W_RUSH] > 0 ||
                  Ude_IsGuardAction(s->my_action);
        char det[96];
        snprintf(det, sizeof det, "threat=%s w=%d/%d/%d/%d", Ude_ThreatName(t->press_threat), s->win[0], s->win[1], s->win[4], s->win[5]);
        event(t, "PRESS", s, in, 0, Ude_ResponseName(t->press_resp), 1, acc, acc ? "accepted" : "not_accepted", det);
        t->accepted += acc;
        t->press_pending = 0;
    }
    /* a contact of the previous frame's hit step */
    if (in->reaction != 1 || (in->evade_flags & 0x3F) || (in->guard_flags & 0x7F) || hp_delta < 0 || in->opp_broken) {
        int age = in->sim_frame - t->last_press_frame;
        const char *def = age <= 6 ? Ude_ResponseName(t->last_press_resp) : t->prev_injected ? "hold" : "none";
        const char *res = result_of(in, hp_delta);
        int acc = strcmp(res, "hit") != 0 && strcmp(res, "none") != 0;
        /* the collision happened during the previous frame: its decision is the one that counts */
        event(t, "CONTACT", s, in, hp_delta, def, age <= 6 ? age : -1, acc, res,
              t->prev_injected ? Ude_ResponseName(t->prev_response) : Ude_ReasonName(t->prev_reason));
        t->contacts++;
    }
    /* conflicts with the player's commands */
    if (manual && t->prev_injected) {
        event(t, "CONFLICT", s, in, 0, "-", -1, -1, "manual_over_injection", "player pressed while guard was being sent; engine released");
        t->conflicts++;
    }
    if (d->injected && s->have_ext && s->my_request_pending && d->press_edge) {
        event(t, "CONFLICT", s, in, 0, "-", -1, -1, "inject_over_request", "must not happen");
        t->conflicts++;
    }
    if (d->press_edge) {
        t->press_pending = 1;
        t->press_frame = in->sim_frame;
        t->press_resp = d->response;
        t->press_threat = d->threat;
        t->last_press_frame = in->sim_frame;
        t->last_press_resp = d->response;
        t->presses++;
    }
    /* the cycle-2 per-frame line, for frames with something to see */
    if (t->log_all || d->injected || d->threat != 0 || s->opp_attacking || (s->have_ext && s->win[0] > UDE_W_FLOOR)) {
        buf_line(&t->frames, "%d,%d,%d,%d,%d,%d,%s,%d,%s,%d,%d,%d,%d,%d,%d,0x%X,%s,%d,%d,%d,0x%X,%d,0x%X,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                 in->sim_frame, in->player, s->my_action, in->my_motion, s->opp_action, in->opp_motion, Ude_ThreatName(d->threat),
                 d->injected, Ude_ReasonName(d->reason), s->close, s->incoming_blast, s->opp_attacking, s->opp_skill_kind, s->my_hp,
                 s->my_ki, physical, Ude_ResponseName(d->response), d->confidence, d->press_edge, s->behind, (unsigned)s->anim_flags,
                 s->opp_attack_id, (unsigned)s->opp_attack_flags, s->opp_hit_active, s->flag5, s->my_request_pending,
                 s->opp_action_frame, s->blast_eta10, s->win[0], s->win[1], s->win[2], s->win[3], s->win[4], s->win[5],
                 s->recover_ok, s->game_free);
    }
    t->prev_hp = s->my_hp;
    t->prev_injected = d->injected;
    t->prev_reason = d->reason;
    t->prev_response = d->response;
    t->prev_my_action = s->my_action;
}

void UdeTel_Flush(UdeTelemetry *t) {
    buf_flush(&t->frames);
    buf_flush(&t->events);
}

void UdeTel_Close(UdeTelemetry *t) {
    UdeTel_Flush(t);
    if (t->frames.f) fclose(t->frames.f);
    if (t->events.f) fclose(t->events.f);
    t->frames.f = t->events.f = NULL;
}
