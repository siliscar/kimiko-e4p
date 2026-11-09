/* SPDX-License-Identifier: Apache-2.0
 * 中文：圆形返回键按下时圆内加底色，只暂存一个控件真实像素并在释放时精确恢复。
 * English: Round backs gain an inner tint on press; one control's actual pixels are retained and restored on release.
 * 冻结：无整帧缓存或旧页回放；返回键释放即恢复，换帧丢弃旧像素。
 * Frozen: No full-frame storage or stale replay; backs restore on release, and new paints discard old pixels.
 * 用户修订：底栏图标按下不再放大或下移，点击只负责切页；图标、文字与横条保持静止。
 * User revision: footer icons no longer enlarge or shift on tap; the tap only navigates, leaving icons, labels and the marker still.
 * 变更原因：放大后复位被读成图标跳动，用户要求底栏完全静止；返回键的按压反馈保留。
 * Why it changed: enlarge-then-settle read as the icon jumping, so the footer stays still; back-button feedback remains.
 */
#include "ui_click_feedback.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "settings.h"
typedef struct { EpdRect area; ui_click_kind_t kind; ui_icon_t icon; } click_control_t;
static click_control_t s_controls[12], s_active;
static unsigned s_count;
static uint8_t *s_pixels;
static uint8_t *s_frame;
static int s_x, s_y;
static EpdRect changed_area(uint8_t *fb);
static void restore_pixels(uint8_t *fb) {
    if (!s_pixels || fb != s_frame) return;
    EpdRect r=s_active.area;
    for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x)
        epd_draw_pixel(r.x+x,r.y+y,s_pixels[y*r.width+x],fb);
}
bool ui_click_feedback_active(void) { return s_pixels != NULL; }
void ui_click_feedback_reset(void) {
    heap_caps_free(s_pixels); s_pixels = NULL; s_count = 0; s_frame = NULL;
}
void ui_click_feedback_begin(uint8_t *fb) {
    // 整页重绘后控件表按新帧重新登记；旧按压像素随上一帧作废。
    // A full repaint re-registers controls for the new frame; old pressed pixels die with the previous frame.
    heap_caps_free(s_pixels); s_pixels = NULL; s_count = 0; s_frame = fb;
}
void ui_click_feedback_register(uint8_t *fb, EpdRect rect, ui_click_kind_t kind, ui_icon_t icon) {
    if (fb != s_frame || !fb) return;
    if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
        rect.width > 64 || rect.height > 64 || rect.x + rect.width > epd_rotated_display_width() ||
        rect.y + rect.height > epd_rotated_display_height()) return;
    for (unsigned i = 0; i < s_count; ++i) if (!memcmp(&s_controls[i].area, &rect, sizeof(rect))) {
        s_controls[i] = (click_control_t){rect, kind, icon}; return;
    }
    if (s_count < 12) s_controls[s_count++] = (click_control_t){rect, kind, icon};
}
static uint8_t read_pixel(uint8_t *fb, int x, int y) {
    int px = x, py = y;
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT: px = epd_width() - y - 1; py = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE: px = epd_width() - x - 1; py = epd_height() - y - 1; break;
        case EPD_ROT_INVERTED_PORTRAIT: px = y; py = epd_height() - x - 1; break;
        default: break;
    }
    return epd_get_pixel(px, py, epd_width(), epd_height(), fb);
}
// 缓存边界不等于推屏边界；未变的底色交给等灰保持，文字不参与反馈。
// Cache bounds are not presentation bounds; unchanged background is held by the equal-gray path, and labels stay out of feedback.
static EpdRect changed_area(uint8_t *fb) {
    EpdRect r = s_active.area;
    int left = r.width, top = r.height, right = -1, bottom = -1;
    for (int y = 0; y < r.height; ++y) for (int x = 0; x < r.width; ++x) {
        if (read_pixel(fb, r.x + x, r.y + y) == s_pixels[y * r.width + x]) continue;
        if (x < left) left = x;
        if (y < top) top = y;
        if (x > right) right = x;
        if (y > bottom) bottom = y;
    }
    return right < 0 ? r : (EpdRect){r.x + left, r.y + top, right - left + 1, bottom - top + 1};
}
bool ui_click_feedback_release(uint8_t *fb, EpdRect *area) {
    if (!s_pixels) return false;
    *area = changed_area(fb);
    restore_pixels(fb);
    heap_caps_free(s_pixels); s_pixels = NULL;
    return true;
}
bool ui_click_feedback_cancel_at(int x, int y) {
    return s_pixels && (x < s_active.area.x || x >= s_active.area.x + s_active.area.width ||
        y < s_active.area.y || y >= s_active.area.y + s_active.area.height ||
        x - s_x > UI_TOUCH_SLOP_PX || s_x - x > UI_TOUCH_SLOP_PX ||
        y - s_y > UI_TOUCH_SLOP_PX || s_y - y > UI_TOUCH_SLOP_PX);
}
bool ui_click_feedback_press(uint8_t *fb, int x, int y, EpdRect *area) {
    if (s_pixels) return false;
    for (unsigned i = 0; i < s_count; ++i) {
        EpdRect r = s_controls[i].area;
        if (!(x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)) continue;
        s_pixels = heap_caps_malloc((size_t)r.width * r.height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pixels) return false;
        s_active = s_controls[i]; s_x = x; s_y = y; *area = r;
        for (int yy = 0; yy < r.height; ++yy) for (int xx = 0; xx < r.width; ++xx)
            s_pixels[yy * r.width + xx] = read_pixel(fb, r.x + xx, r.y + yy);
        int cx = r.x + r.width / 2, cy = r.y + r.height / 2;
        epd_fill_rect(r, s_pixels[0], fb);
        // 返回图标包含完整圆环；按压只增加圆内底色，不另画圆环或移动题头按钮。
        // The icon includes its ring; press tint stays inside without another outline or shifted header geometry.
        bool small=s_active.kind==UI_CLICK_BACK_SMALL;
        int diameter=small?40:44,offset=small?0:1;
        ui_fill_round_rect(fb,(EpdRect){cx-diameter/2,cy-diameter/2+offset,diameter,diameter},diameter/2,0xd0);
        ui_draw_icon(fb,cx,cy+offset,small?48:52,s_active.icon,UI_GRAY_BLACK);
        if (app_settings_main_fast_refresh()) ui_image_bw_rect(fb, r);
        *area = changed_area(fb);
        return true;
    }
    return false;
}
