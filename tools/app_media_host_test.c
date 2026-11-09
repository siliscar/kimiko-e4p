/* 中文：验证真实介质服务的通知、停止顺序与恢复边界。/ English: Verify notification, shutdown ordering and recovery boundaries in the real media service. */
#include "app_media.h"
#include "board_sd.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static board_sd_info_t info;
static int status;
static bool usb, font_ready, builtin, saved_builtin, open_failed;
static bool quiesce_ok = true;
static bool avatar_ok = true;
static char events[64];
static size_t used;
static uint64_t trace = UINT64_C(14695981039346656037);
static uint32_t rng = 0x531;
static unsigned remounts, probes, opens, lost_count, ready_count;

static void record(unsigned value) {
    trace = (trace ^ value) * UINT64_C(1099511628211);
}
static void event(char value) {
    assert(used + 1 < sizeof(events));
    events[used++] = value;
    events[used] = 0;
    record((unsigned)value);
}
static void clear_events(void) { used = 0; events[0] = 0; }
static void expect(const char *expected) {
    if (strcmp(events, expected) != 0) fprintf(stderr, "media events: got %s, expected %s\n", events, expected);
    assert(strcmp(events, expected) == 0);
    clear_events();
}
static uint32_t random_value(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
bool usb_storage_active(void) { return usb; }
int board_sd_get_info(board_sd_info_t *out) { event('I'); *out = info; return status; }
void board_sd_start_probe(void) { event('P'); ++probes; }
int board_sd_remount(void) { event('M'); ++remounts; return ESP_ERR_NOT_FINISHED; }
bool ttf_font_ready(void) { return font_ready; }
bool ttf_font_is_builtin(void) { return builtin; }
bool ttf_font_path_is_builtin(const char *path) { (void)path; return saved_builtin; }
int ttf_font_open(const char *path) {
    assert(strcmp(path, "saved") == 0);
    event('F'); ++opens;
    if (!open_failed) { font_ready = true; builtin = false; }
    return open_failed ? -1 : ESP_OK;
}
int ttf_font_open_builtin(void) { event('B'); builtin = font_ready = true; return ESP_OK; }
void ui_avatar_invalidate(void) { event('A'); }
bool book_cover_jobs_quiesce(unsigned timeout_ms) { assert(timeout_ms == 0); event('Q'); return quiesce_ok; }
bool ui_avatar_quiesce(unsigned timeout_ms) { assert(timeout_ms == 0); event('V'); return avatar_ok; }
void book_lock_collage_invalidate(void) { event('C'); }
void ui_text_set_system_font(bool active) { assert(!active); event('S'); }
static void lost(app_ctx_t *ctx) { assert(ctx); event('L'); ++lost_count; }
static void ready(app_ctx_t *ctx) { assert(ctx); event('R'); ++ready_count; }

int main(void) {
    app_ctx_t ctx = {.now_ms = 2000};
    app_desc_t page = {.on_media_lost = lost, .on_media_ready = ready};
    bool mounted = false, invalidated = false;
    // 开机时尚存的卡上字体同样代表已有消费者，即使卡暂未挂载。
    // A live card font represents an existing consumer even before the mount is ready.
    assert(!app_media_initial_mounted()); expect("I");
    font_ready = true; builtin = false;
    assert(app_media_initial_mounted()); expect("I");
    builtin = true;
    assert(!app_media_initial_mounted()); expect("I");
    info.mounted = true;
    assert(app_media_initial_mounted()); expect("I");
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IAQCR");
    assert(mounted && !invalidated && ready_count == 1);
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("I");
    // 正在探测时保留旧状态；不可把暂未完成误报为拔卡。
    // Preserve the old state during probing; unfinished work is not removal.
    status = ESP_ERR_NOT_FINISHED; info.mounted = false;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("I");
    assert(mounted && !invalidated);
    // 停封面工作先于页面关闭，页面先于字体回退；不重复通知。
    // Quiesce covers before closing the page, then fall back the face; never notify twice.
    status = -9; builtin = false; quiesce_ok = false;
    assert(app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IAQCLBS");
    assert(!mounted && invalidated && lost_count == 1);
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("I");
    info.present = true;
    // 重挂前两个后台使用者都必须交还；失败不重复通知页面，也不访问卡。
    // Both background consumers must hand back before remount; failures neither renotify the page nor touch the card.
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IQ");
    assert(!remounts && !mounted && invalidated && lost_count == 1);
    ctx.now_ms = 3999;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("I");
    ctx.now_ms = 4000;
    quiesce_ok = true; avatar_ok = false;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IQV");
    assert(!remounts && !mounted && invalidated && lost_count == 1);
    ctx.now_ms = 5999;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("I");
    ctx.now_ms = 6000; avatar_ok = true;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IQVM");
    assert(remounts == 1 && !mounted && invalidated && lost_count == 1);
    ctx.now_ms = 7999;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("I");
    ctx.now_ms = 8000;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IQVM");
    assert(remounts == 2);
    status = ESP_OK; info.mounted = true;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IAQCR");
    assert(mounted && !invalidated && ready_count == 2);
    // USB 所有权期间不访问卡、不发失效通知；退出后走已有就绪通知。
    // Under USB ownership, never access the card or signal loss; exiting uses the existing ready notification.
    usb = true; invalidated = true;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("");
    assert(!mounted && !invalidated);
    usb = false;
    assert(!app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IAQCR");
    // 没有页面回调及尚无可用字体时，仍正确传播失效并选择系统回退。
    // Without page callbacks or a ready face, propagate loss and select the system fallback.
    page.on_media_lost = NULL; page.on_media_ready = NULL;
    info.present = info.mounted = false; font_ready = false;
    assert(app_media_poll(&ctx, &page, &mounted, &invalidated)); expect("IAQCS");
    assert(!mounted && invalidated);
    // 字体探测受允许标志控制；未完成、内建选择和打开失败均不会报告成功。
    // The probe-permission flag gates font probing; unfinished work, a built-in selection and failed opens never report success.
    status = ESP_ERR_INVALID_STATE;
    assert(!app_media_retry_font(false)); expect("I");
    assert(!app_media_retry_font(true)); expect("IP");
    status = ESP_ERR_NOT_FINISHED; info.mounted = true;
    assert(!app_media_retry_font(true)); expect("I");
    status = ESP_OK; builtin = true; saved_builtin = true;
    assert(!app_media_retry_font(true)); expect("I");
    saved_builtin = false; open_failed = true;
    assert(!app_media_retry_font(true)); expect("IF");
    open_failed = false;
    assert(app_media_retry_font(true)); expect("IF");
    assert(!app_media_retry_font(true)); expect("I");
    // 固定种子驱动混合介质、USB、字体失败与页面回调，供旧新服务对比事件轨迹。
    // Seed mixed media, USB, font failures and page callbacks for old/new event-trace comparison.
    for (unsigned i = 0; i < 100000; ++i) {
        uint32_t r = random_value();
        ctx.now_ms += r % 2101;
        info.present = (r & 1) != 0; info.mounted = (r & 2) != 0;
        static const int errors[] = {ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_NOT_FINISHED, -9};
        status = errors[(r >> 2) & 3]; usb = ((r >> 4) & 7) == 0;
        font_ready = (r & 128) != 0; builtin = (r & 256) != 0;
        saved_builtin = (r & 512) != 0; open_failed = (r & 1024) != 0;
        quiesce_ok = (r & 2048) != 0;
        avatar_ok = (r & 65536) != 0;
        page.on_media_lost = (r & 4096) ? lost : NULL;
        page.on_media_ready = (r & 8192) ? ready : NULL;
        clear_events();
        if ((r >> 14) & 1) record(app_media_initial_mounted());
        record(app_media_poll(&ctx, &page, &mounted, &invalidated));
        record(mounted); record(invalidated);
        if ((r >> 15) & 1) record(app_media_retry_font(!invalidated));
        record(font_ready); record(builtin);
    }
    printf("MEDIA_TRACE %016" PRIx64 " remounts=%u probes=%u opens=%u lost=%u ready=%u\n",
           trace, remounts, probes, opens, lost_count, ready_count);
    puts("PASS: media loss ordering, mount recovery/throttling, USB ownership, optional callbacks and font retry boundaries");
    return 0;
}
