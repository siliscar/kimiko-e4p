/*
 * 中文：运行真实阅读重排事务，注入构建及回滚失败，核对资源交还和持久化边界。
 * English: Exercise real reflow transactions with build/rollback failures and verify handoff and persistence boundaries.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reader_reflow.h"
#include "book_layout.h"
#include "settings.h"

static reader_position_t position;
static int px, margins[4], line_spacing;
static uint8_t stored_px, stored_margins[4], stored_line, stored_paragraph, stored_tracking, stored_indent;
static int8_t stored_adjustment;
static int layout_line, layout_paragraph, layout_slack, layout_tracking, layout_indent, layout_adjustment;
static size_t span;
static bool locked, handed_back;
static unsigned begins, ends, leads, builds, maps, writes, saves, failures;
static bool statusbar, hide_images, vertical, images_visible, layout_vertical, fullscreen, mode_test;

static void require_layout_access(void) { assert(locked && handed_back); }
static void begin(void) { assert(!locked && !handed_back); locked = handed_back = true; ++begins; }
static void end(void) { require_layout_access(); locked = handed_back = false; ++ends; }
static void lead(void) { require_layout_access(); assert(layout_slack == margins[1]); ++leads; }
static void persist(void) { assert(!locked && !handed_back && ends == 1 && writes); ++saves; }
static void write_setting(void) { assert(!locked && !handed_back && ends == 1 && builds); ++writes; }
static bool build(void) {
    require_layout_access(); ++builds;
    if (failures) { --failures; span = 0; return false; }
    span = (size_t)(px * 2 + layout_tracking + layout_indent * 10 + layout_adjustment);
    if (mode_test) span += statusbar * 10 + hide_images * 20 + vertical * 30 + fullscreen * 40;
    return true;
}
static const reader_reflow_t reflow = {
    .position = &position, .px = &px, .margins = margins, .line_spacing = &line_spacing,
    .begin = begin, .end = end, .refresh_lead = lead, .build = build, .save = persist,
};

size_t book_layout_page_start_offset(size_t page) { assert(!locked && !begins); return page * span; }
size_t book_layout_page_for_offset(size_t offset) { require_layout_access(); assert(span); ++maps; return offset / span; }
void book_layout_set_spacing(unsigned line, unsigned paragraph) { require_layout_access(); layout_line = line; layout_paragraph = paragraph; }
void book_layout_set_right_slack(int slack) { require_layout_access(); layout_slack = slack; }
void book_layout_set_typography(int tracking) { require_layout_access(); layout_tracking = tracking; }
void book_layout_set_first_line_indent(unsigned em) { require_layout_access(); layout_indent = em; }
void book_layout_set_first_line_indent_adjust(int adjustment) { require_layout_access(); layout_adjustment = adjustment; }
uint8_t app_settings_book_line_spacing(void) { return stored_line; }
uint8_t app_settings_book_paragraph_spacing(void) { return stored_paragraph; }
uint8_t app_settings_book_tracking(void) { return stored_tracking; }
uint8_t app_settings_book_indent(void) { return stored_indent; }
int8_t app_settings_book_indent_adjust(void) { return stored_adjustment; }
static const uint8_t paragraph_stops[] = {0, 5, 10, 15, 20, 25, 30, 40, 50, 75, 100};
uint8_t app_settings_book_paragraph_value(uint8_t level) { return paragraph_stops[level]; }
uint8_t app_settings_book_paragraph_level(uint8_t percent) {
    for (uint8_t i = 0; i < sizeof(paragraph_stops); ++i) if (paragraph_stops[i] == percent) return i;
    return 0;
}
void app_settings_set_book_px(uint8_t value) { write_setting(); stored_px = value; }
void app_settings_set_book_margin_left(uint8_t value) { write_setting(); stored_margins[0] = value; }
void app_settings_set_book_margin_right(uint8_t value) { write_setting(); stored_margins[1] = value; }
void app_settings_set_book_margin_top(uint8_t value) { write_setting(); stored_margins[2] = value; }
void app_settings_set_book_margin_bottom(uint8_t value) { write_setting(); stored_margins[3] = value; }
void app_settings_set_book_line_spacing(uint8_t value) { write_setting(); stored_line = value; }
void app_settings_set_book_paragraph_spacing(uint8_t value) { write_setting(); stored_paragraph = value; }
void app_settings_set_book_tracking(uint8_t value) { write_setting(); stored_tracking = value; }
void app_settings_set_book_indent(uint8_t value) { write_setting(); stored_indent = value; }
void app_settings_set_book_indent_adjust(int8_t value) { write_setting(); stored_adjustment = value; }
void app_settings_set_reader_statusbar(bool value) { require_layout_access(); ++writes; statusbar = value; }
void app_settings_set_reader_hide_images(bool value) { require_layout_access(); ++writes; hide_images = value; }
void app_settings_set_book_vertical(bool value) { require_layout_access(); ++writes; vertical = value; }
void book_layout_set_images_visible(bool value) { require_layout_access(); assert(value == !hide_images); images_visible = value; }
void book_layout_set_vertical(bool value) { require_layout_access(); assert(value == vertical); layout_vertical = value; }

static void reset(void) {
    position = (reader_position_t){7, 2};
    px = stored_px = 48; line_spacing = stored_line = layout_line = 130;
    margins[0] = margins[1] = stored_margins[0] = stored_margins[1] = layout_slack = 24;
    margins[2] = margins[3] = stored_margins[2] = stored_margins[3] = 0;
    stored_paragraph = layout_paragraph = 25; stored_tracking = 15; stored_indent = layout_indent = 2;
    stored_adjustment = layout_tracking = layout_adjustment = 0;
    span = 100; locked = handed_back = false;
    begins = ends = leads = builds = maps = writes = saves = failures = 0;
    statusbar = images_visible = true;
    hide_images = vertical = layout_vertical = fullscreen = mode_test = false;
}

int main(void) {
    const int next[] = {18, 36, 6, 12};
    reset();
    assert(reader_reflow_layout(&reflow, 59, next, 135, 50));
    assert(px == 59 && stored_px == 59 && !memcmp(margins, next, sizeof(next)));
    for (int i = 0; i < 4; ++i) assert(stored_margins[i] == next[i]);
    assert(position.chapter == 7 && position.page == 200 / span && maps == 1);
    assert(begins == 1 && ends == 1 && leads == 1 && builds == 1 && writes == 7 && saves == 1);
    assert(layout_line == 135 && layout_paragraph == 50 && layout_slack == 36);

    for (unsigned fail = 1; fail <= 2; ++fail) {
        reset(); failures = fail;
        assert(!reader_reflow_layout(&reflow, 59, next, 135, 50));
        assert(px == 48 && stored_px == 48 && margins[0] == 24 && margins[1] == 24 && !margins[2] && !margins[3]);
        assert(line_spacing == 130 && layout_line == 130 && layout_paragraph == 25 && layout_slack == 24);
        assert(position.chapter == 7 && position.page == 2 && !maps && !writes && !saves);
        assert(builds == 2 && leads == 2 && begins == 1 && ends == 1 && !locked && !handed_back);
        assert((span == 0) == (fail == 2));
    }
    reset(); assert(!reader_reflow_layout(&reflow, 48, margins, 130, 25) && !begins);
    assert(!reader_reflow_layout(&reflow, 17, next, 130, 25));
    assert(!reader_reflow_layout(&reflow, 91, next, 130, 25));
    assert(!reader_reflow_layout(&reflow, 48, next, 109, 25));
    assert(!reader_reflow_layout(&reflow, 48, next, 151, 25));
    assert(!reader_reflow_layout(&reflow, 48, next, 130, 1));
    for (int i = 0; i < 4; ++i) {
        int bad[4] = {0}; bad[i] = -1;
        assert(!reader_reflow_layout(&reflow, 48, bad, 130, 25));
        bad[i] = 97; assert(!reader_reflow_layout(&reflow, 48, bad, 130, 25));
    }
    assert(!begins && !writes && !saves);
    reset(); assert(reader_reflow_layout(&reflow, 48, margins, 135, 50));
    assert(margins[0] == 24 && margins[1] == 24 && writes == 7 && saves == 1);

    reset(); assert(reader_reflow_typography(&reflow, 30));
    assert(stored_tracking == 30 && layout_tracking == 15 && position.page == 200 / span);
    assert(builds == 1 && maps == 1 && writes == 1 && saves == 1 && !leads && !locked);
    reset(); assert(reader_reflow_indent(&reflow, 3, 10));
    assert(stored_indent == 3 && stored_adjustment == 10 && layout_indent == 3 && layout_adjustment == 10);
    assert(position.page == 200 / span && builds == 1 && maps == 1 && writes == 2 && saves == 1 && !leads);
    for (unsigned fail = 1; fail <= 2; ++fail) {
        reset(); failures = fail;
        assert(!reader_reflow_typography(&reflow, 30));
        assert(stored_tracking == 15 && !layout_tracking && !writes && !saves && !maps);
        assert(builds == 2 && ends == 1 && position.page == 2 && !locked);
        reset(); failures = fail;
        assert(!reader_reflow_indent(&reflow, 3, 10));
        assert(stored_indent == 2 && !stored_adjustment && layout_indent == 2 && !layout_adjustment);
        assert(builds == 2 && ends == 1 && position.page == 2 && !writes && !saves && !maps && !locked);
    }
    reset();
    assert(!reader_reflow_typography(&reflow, -1) && !reader_reflow_typography(&reflow, 31));
    assert(!reader_reflow_typography(&reflow, 15));
    assert(!reader_reflow_indent(&reflow, -1, 0) && !reader_reflow_indent(&reflow, 4, 0));
    assert(!reader_reflow_indent(&reflow, 2, -101) && !reader_reflow_indent(&reflow, 2, 101));
    assert(!reader_reflow_indent(&reflow, 2, 0) && !begins && !writes && !saves);
    for (int kind = 0; kind < 3; ++kind) for (int previous = 0; previous < 2; ++previous)
        for (unsigned fail = 0; fail <= 2; ++fail) {
            reset(); mode_test = true; failures = fail;
            statusbar = hide_images = vertical = layout_vertical = previous;
            images_visible = !hide_images;
            size_t offset = book_layout_page_start_offset(position.page);
            begin();
            assert(reader_reflow_option_locked(&reflow, kind, previous, offset) == (fail == 0));
            assert(locked && handed_back && !ends && !saves);
            assert(writes == (fail ? 2u : 1u) && builds == writes && leads == builds);
            bool value = kind == 0 ? statusbar : kind == 1 ? hide_images : vertical;
            assert(value == (fail ? previous : !previous));
            assert(images_visible == !hide_images && layout_vertical == vertical);
            assert(position.chapter == 7 && position.page == (fail ? 2 : offset / span));
            assert(maps == (fail ? 0u : 1u) && (span == 0) == (fail == 2));
            end();
        }
    for (int previous = 0; previous < 2; ++previous) for (unsigned fail = 0; fail <= 2; ++fail) {
        reset(); mode_test = true; fullscreen = previous; failures = fail;
        reader_reflow_result_t expected = fail == 0 ? READER_REFLOW_APPLIED :
            fail == 1 ? READER_REFLOW_RESTORED : READER_REFLOW_LOST_LAYOUT;
        assert(reader_reflow_fullscreen(&reflow, &fullscreen) == expected);
        assert(fullscreen == (fail ? previous : !previous));
        assert(position.chapter == 7 && position.page == (fail ? 2 : 200 / span));
        assert(begins == 1 && ends == 1 && !locked && !handed_back && !saves && !writes);
        assert(builds == (fail ? 2u : 1u) && leads == builds && maps == (fail ? 0u : 1u));
        assert((span == 0) == (fail == 2));
    }
    puts("PASS: real setting/mode/fullscreen reflow, handoff/lock boundaries, anchored commits, failed restore and persistence");
}
