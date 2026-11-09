/*
 * 中文：电量日志的可重放统计与有界文件输出；累计值是采样估算，空白和锁屏不外推。
 * English: Replayable battery logging and bounded file output; totals are sampled estimates without extrapolating gaps or locked waits.
 */
#include "battery_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void enqueue(batt_log_state_t *s, uint32_t events, int64_t now, int64_t wall) {
    if (s->count == BATT_LOG_QUEUE_SIZE) {
        s->head = (s->head + 1) % BATT_LOG_QUEUE_SIZE;
        --s->count;
        ++s->dropped;
    }
    s->queue[(s->head + s->count++) % BATT_LOG_QUEUE_SIZE] = (batt_log_record_t){
        .sample = s->latest, .have_sample = s->have_sample, .locked = s->locked,
        .session = s->session, .seq = ++s->seq, .events = events, .dropped = s->dropped,
        .up_us = now, .wall_sec = wall, .covered_us = s->covered_us,
        .unknown_us = s->unknown_us, .locked_us = s->locked_us,
        .in_ma_us = s->in_ma_us, .out_ma_us = s->out_ma_us,
    };
    s->last_emit_us = now;
}

// 分别积分正负电流；跨零时按线性过零点分割，不能用净值抵消。
// Integrate each current direction separately, splitting linear zero crossings rather than cancelling net charge.
static void integrate(batt_log_state_t *s, int64_t dt, int32_t a, int32_t b) {
    if (a >= 0 && b >= 0) s->in_ma_us += (a + b) * dt / 2;
    else if (a <= 0 && b <= 0) s->out_ma_us -= (a + b) * dt / 2;
    else {
        const int64_t aa = a < 0 ? -(int64_t)a : a;
        const int64_t bb = b < 0 ? -(int64_t)b : b;
        const int64_t first = aa * aa * dt / (2 * (aa + bb));
        const int64_t second = bb * bb * dt / (2 * (aa + bb));
        s->in_ma_us += a > 0 ? first : second;
        s->out_ma_us += a < 0 ? first : second;
    }
}

void batt_log_set_enabled(batt_log_state_t *s, bool enabled, int64_t now, int64_t wall) {
    if (s->enabled == enabled) return;
    if (!enabled) {
        batt_log_note(s, BATT_LOG_END, now, wall);
        s->enabled = false;
        return;
    }
    s->enabled = true;
    ++s->session;
    s->have_sample = s->previous_valid = false;
    s->have_cursor = true;
    s->cursor_us = now;
    s->covered_us = s->unknown_us = s->locked_us = 0;
    s->in_ma_us = s->out_ma_us = 0;
    enqueue(s, BATT_LOG_BEGIN, now, wall);
}

void batt_log_observe(batt_log_state_t *s, const batt_log_sample_t *sample) {
    if (!s->enabled) return;
    uint32_t events = 0;
    const bool valid = sample->have_v && sample->have_i;
    // 保护事件可能插在读数与发布之间；旧采样可保留，但不能倒退积分游标。
    // Protection events may occur between a read and publication; retain the sample without rewinding accounting.
    const bool ordered = !s->have_cursor || sample->up_us >= s->cursor_us;
    const int64_t now = ordered ? sample->up_us : s->cursor_us;
    if (s->have_cursor && sample->up_us > s->cursor_us) {
        const int64_t dt = sample->up_us - s->cursor_us;
        if (!s->locked && valid && s->previous_valid && dt <= BATT_LOG_MAX_INTERVAL_US) {
            integrate(s, dt, s->latest.ma, sample->ma);
            s->covered_us += dt;
        } else {
            s->unknown_us += dt;
            if (s->locked) s->locked_us += dt;
            else if (dt > BATT_LOG_MAX_INTERVAL_US) events |= BATT_LOG_GAP;
        }
    }
    if (!s->have_sample) events |= BATT_LOG_SAMPLE;
    else {
        const batt_log_sample_t *p = &s->latest;
        if (p->have_v != sample->have_v || p->have_i != sample->have_i ||
            p->have_chg != sample->have_chg) events |= BATT_LOG_VALIDITY;
        if (p->charging != sample->charging ||
            (p->have_chg && sample->have_chg && p->charger_status != sample->charger_status))
            events |= BATT_LOG_CHARGER;
        if (p->charge_done != sample->charge_done || p->done_source != sample->done_source)
            events |= BATT_LOG_FULL;
    }
    s->latest = *sample;
    s->have_sample = s->have_cursor = true;
    s->previous_valid = valid && !s->locked && ordered;
    s->cursor_us = now;
    if (!s->locked && now - s->last_emit_us >= BATT_LOG_PERIOD_US)
        events |= BATT_LOG_SAMPLE;
    if (events) enqueue(s, events, now, sample->wall_sec);
}

void batt_log_note(batt_log_state_t *s, uint32_t events, int64_t now, int64_t wall) {
    if (s->have_cursor && now < s->cursor_us) now = s->cursor_us;
    if (events & BATT_LOG_LOCK) s->previous_valid = false;
    if (s->enabled && s->have_cursor && now > s->cursor_us) {
        const int64_t dt = now - s->cursor_us;
        s->unknown_us += dt;
        if (s->locked) s->locked_us += dt;
    }
    if (events & BATT_LOG_LOCK) s->locked = true;
    if (events & BATT_LOG_WAKE) s->locked = false;
    if (!s->enabled) return;
    s->have_cursor = true;
    s->cursor_us = now;
    s->previous_valid = false;
    enqueue(s, events, now, wall);
}

bool batt_log_pop(batt_log_state_t *s, batt_log_record_t *record) {
    if (!s->count) return false;
    *record = s->queue[s->head];
    s->head = (s->head + 1) % BATT_LOG_QUEUE_SIZE;
    --s->count;
    return true;
}

int batt_log_format(char *line, size_t size, const batt_log_record_t *r, uint64_t boot,
                    int64_t write_us, uint32_t io_errors, uint32_t file_missed) {
    const batt_log_sample_t *s = &r->sample;
    const bool v = r->have_sample && s->have_v;
    const bool i = r->have_sample && s->have_i;
    const bool chg = r->have_sample && s->have_chg;
    const int flags = (v ? 1 : 0) | (i ? 2 : 0) | (chg ? 4 : 0) |
                      (r->have_sample && s->ic_soc >= 0 ? 8 : 0);
    const int n = snprintf(line, size,
        "fmt=2 boot=%016llx session=%u seq=%u event=%u t=%lld up_ms=%lld "
        "sample_t=%lld sample_ms=%lld age_ms=%lld write_ms=%lld read_us=%u valid=%d "
        "raw=%d ma=%d chg=%d done=%d done_src=%u cx=%d comp=%ld ocv=%ld curve=%ld disp=%ld "
        "ic_soc=%d ic_age_ms=%lld locked=%d covered_ms=%lld unknown_ms=%lld lock_ms=%lld "
        "in_uah_est=%lld out_uah_est=%lld dropped=%u io_errors=%u file_missed=%u\n",
        (unsigned long long)boot, (unsigned)r->session, (unsigned)r->seq, (unsigned)r->events,
        (long long)r->wall_sec, (long long)(r->up_us / 1000),
        r->have_sample ? (long long)s->wall_sec : -1LL,
        r->have_sample ? (long long)(s->up_us / 1000) : -1LL,
        r->have_sample ? (long long)((write_us - s->up_us) / 1000) : -1LL,
        (long long)(write_us / 1000), r->have_sample ? (unsigned)s->read_us : 0u, flags,
        v ? (int)s->raw_mv : -1, i ? (int)s->ma : -1,
        r->have_sample && (chg || i) ? (int)s->charging : -1,
        r->have_sample && v && (chg || i) ? (int)s->charge_done : -1,
        v ? (unsigned)s->done_source : 0u, chg ? (int)s->charger_status : -1,
        v ? (long)s->comp_mv : -1L, v ? (long)s->ocv_mv : -1L,
        v ? (long)s->curve_permille : -1L, v ? (long)s->disp_permille : -1L,
        r->have_sample ? (int)s->ic_soc : -1,
        r->have_sample && s->ic_soc >= 0 ? (long long)((write_us - s->ic_us) / 1000) : -1LL,
        (int)r->locked, (long long)(r->covered_us / 1000), (long long)(r->unknown_us / 1000),
        (long long)(r->locked_us / 1000), (long long)(r->in_ma_us / 3600000),
        (long long)(r->out_ma_us / 3600000), (unsigned)r->dropped,
        (unsigned)io_errors, (unsigned)file_missed);
    return n > 0 && (size_t)n < size ? n : -1;
}

bool batt_log_append(batt_log_file_t *file, const char *path, const char *backup,
                     long limit, uint32_t session, const char *metadata, const char *line) {
    struct stat st;
    bool empty = false;
    if (stat(path, &st) != 0) {
        if (errno != ENOENT) { file->header = true; return false; }
        empty = true;
    } else if (st.st_size >= limit) {
        if (remove(backup) != 0 && errno != ENOENT) { file->header = true; return false; }
        if (rename(path, backup) != 0) { file->header = true; return false; }
        empty = true;
    } else empty = st.st_size == 0;
    FILE *f = fopen(path, "a");
    if (!f) { file->header = true; return false; }
    bool ok = true;
    if (empty || file->header || file->session != session) {
        ok = fprintf(f, "\n# battlog fmt=2 session=%u %s\n", (unsigned)session, metadata) > 0;
    }
    if (ok) ok = fputs(line, f) != EOF;
    if (fflush(f) != 0) ok = false;
    if (fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    file->header = !ok;
    if (ok) file->session = session;
    return ok;
}
