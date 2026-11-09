/*
 * 中文：验证真实翻页模块的保存/加载顺序、失败位置归属与帧效果边界。
 * English: Verify real turn control save/load order, failed-position ownership and frame-effect boundaries.
 */
#include <assert.h>
#include <stdio.h>
#include "reader_turn.h"

typedef struct {
    reader_position_t position;
    reader_position_t saved;
    unsigned saves, loads;
    size_t loaded_chapter;
    bool loaded_last, fail, discard;
} fixture_t;

static void save(void *context) {
    fixture_t *f = context;
    assert(f->saves == 0 && f->loads == 0);
    f->saved = f->position;
    ++f->saves;
}

static bool load(void *context, size_t chapter, bool last) {
    fixture_t *f = context;
    assert(f->saves == 1 && f->loads == 0);
    assert(f->position.chapter == f->saved.chapter && f->position.page == f->saved.page);
    ++f->loads;
    f->loaded_chapter = chapter;
    f->loaded_last = last;
    if (!f->fail || f->discard) f->position = (reader_position_t){chapter, last ? 6 : 0};
    return !f->fail;
}

static reader_turn_result_t move(fixture_t *f, int direction) {
    reader_turn_ops_t ops = {.context = f, .save = save, .load = load};
    return reader_turn_move(&f->position, direction, 4, 3, &ops);
}

int main(void) {
    fixture_t f = {.position = {1, 1}};
    assert(move(&f, 1) == READER_TURN_MOVED && f.position.page == 2);
    assert(move(&f, -1) == READER_TURN_MOVED && f.position.page == 1);
    assert(f.position.chapter == 1 && f.saves == 0 && f.loads == 0);
    f = (fixture_t){.position = {1, 3}};
    assert(move(&f, 1) == READER_TURN_MOVED);
    assert(f.saves == 1 && f.loads == 1 && f.saved.chapter == 1 && f.saved.page == 3);
    assert(f.loaded_chapter == 2 && !f.loaded_last && f.position.chapter == 2 && f.position.page == 0);
    f = (fixture_t){.position = {1, 0}};
    assert(move(&f, -1) == READER_TURN_MOVED);
    assert(f.loaded_chapter == 0 && f.loaded_last && f.position.chapter == 0 && f.position.page == 6);
    for (int failed = 0; failed < 2; ++failed) {
        f = (fixture_t){.position = {1, 3}, .fail = true, .discard = failed};
        assert(move(&f, 1) == READER_TURN_LOAD_FAILED && f.saves == 1 && f.loads == 1);
        // 加载器可能保留旧布局或已提交失败位置，控制器不得擅自回滚。
        // A loader may retain old layout or commit a failed position; control must not roll it back.
        assert(f.position.chapter == (failed ? 2 : 1) && f.position.page == (failed ? 0 : 3));
    }
    f = (fixture_t){.position = {0, 0}};
    assert(move(&f, -1) == READER_TURN_EDGE && f.saves == 0 && f.loads == 0);
    f = (fixture_t){.position = {2, 3}};
    assert(move(&f, 1) == READER_TURN_EDGE && f.saves == 0 && f.loads == 0);
    assert(move(&f, 0) == READER_TURN_EDGE && f.position.chapter == 2 && f.position.page == 3);

    reader_turn_frame_t frame = {.area_redraw = true, .from_text = true,
        .from_stable = true, .water_effect = true, .chapter_changed = true};
    reader_turn_effects_t effect = reader_turn_effects(&frame);
    assert(effect.text && effect.water && effect.footer_strong);
    frame.background = true;
    effect = reader_turn_effects(&frame);
    assert(!effect.text && effect.water && effect.footer_strong);
    frame.images = true;
    effect = reader_turn_effects(&frame);
    assert(!effect.text && !effect.water && effect.footer_strong);
    frame.images = frame.background = false;
    frame.cleanup = true;
    effect = reader_turn_effects(&frame);
    assert(effect.text && !effect.water && !effect.footer_strong);
    frame.cleanup = false;
    frame.chrome_changed = true;
    effect = reader_turn_effects(&frame);
    assert(!effect.text && !effect.water && !effect.footer_strong);
    frame.chrome_changed = false;
    frame.from_text = frame.from_stable = false;
    effect = reader_turn_effects(&frame);
    assert(!effect.text && !effect.water && effect.footer_strong);
    frame.from_text = frame.from_stable = true;
    frame.area_redraw = false;
    effect = reader_turn_effects(&frame);
    assert(!effect.text && !effect.water && effect.footer_strong);
    puts("PASS: real turn control order, loader-owned failure positions, book edges and frame effects");
}
